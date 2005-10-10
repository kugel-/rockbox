/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2005 Thom Johansen
 *
 * All files in this archive are subject to the GNU General Public License.
 * See the file COPYING in the source tree root for full license agreement.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/

#include "codec.h"
#include "lib/codeclib.h"
#include <codecs/libmusepack/musepack.h>

mpc_decoder decoder;

/* Our implementations of the mpc_reader callback functions. */
mpc_int32_t read_impl(void *data, void *ptr, mpc_int32_t size)
{
    struct codec_api *ci = (struct codec_api *)data;

    return ((mpc_int32_t)(ci->read_filebuf(ptr, size)));
}

bool seek_impl(void *data, mpc_int32_t offset)
{  
    struct codec_api *ci = (struct codec_api *)data;

    /* WARNING: assumes we don't need to skip too far into the past,
       this might not be supported by the buffering layer yet */
    return ci->seek_buffer(offset);
}

mpc_int32_t tell_impl(void *data)
{
    struct codec_api *ci = (struct codec_api *)data;

    return ci->curpos;
}

mpc_int32_t get_size_impl(void *data)
{
    struct codec_api *ci = (struct codec_api *)data;
    
    return ci->filesize;
}

bool canseek_impl(void *data)
{
    (void)data;
    
    /* doesn't much matter, libmusepack ignores this anyway */
    return true;
}

/* temporary, we probably have better use for iram than this */
MPC_SAMPLE_FORMAT sample_buffer[MPC_DECODER_BUFFER_LENGTH] IDATA_ATTR;

#ifdef USE_IRAM
extern char iramcopy[];
extern char iramstart[];
extern char iramend[];
#endif

/* this is the codec entry point */
enum codec_status codec_start(struct codec_api *api)
{
    struct codec_api *ci = api;
    mpc_int64_t samplesdone;
    unsigned long frequency;
    unsigned status = 1;
    mpc_reader reader;
    mpc_streaminfo info;
    
    TEST_CODEC_API(api);
    #ifdef USE_IRAM 
    ci->memcpy(iramstart, iramcopy, iramend - iramstart);
    #endif
    
    ci->configure(CODEC_DSP_ENABLE, (bool *)true);
    ci->configure(DSP_DITHER, (bool *)false);
    ci->configure(DSP_SET_SAMPLE_DEPTH, (long *)(MPC_FIXED_POINT_SCALE_SHIFT - 1));
    ci->configure(DSP_SET_CLIP_MAX, (long *)MPC_FIXED_POINT_SCALE);
    ci->configure(DSP_SET_CLIP_MIN, (long *)-MPC_FIXED_POINT_SCALE);
    ci->configure(CODEC_SET_FILEBUF_LIMIT, (long *)(1024*1024*2));
    ci->configure(CODEC_SET_FILEBUF_CHUNKSIZE, (long *)(1024*16));
    
    /* Create a decoder instance */
    reader.read = read_impl;
    reader.seek = seek_impl;
    reader.tell = tell_impl;
    reader.get_size = get_size_impl;
    reader.canseek = canseek_impl;
    reader.data = ci;

next_track:    
    if (codec_init(api))
        return CODEC_ERROR;

    /* read file's streaminfo data */
    mpc_streaminfo_init(&info);
    if (mpc_streaminfo_read(&info, &reader) != ERROR_CODE_OK)
        return CODEC_ERROR;  
    frequency = info.sample_freq;
    ci->configure(DSP_SET_FREQUENCY, (long *)info.sample_freq);
        
    /* TODO: this should no doubt be handled in metadata.c */
    ci->id3->length = info.pcm_samples/info.sample_freq*1000;
        
    /* set playback engine up for correct number of channels */
    /* NOTE: current musepack format only allows for stereo files
       but code is here to handle other configurations anyway */
    if (info.channels == 2)
        ci->configure(DSP_SET_STEREO_MODE, (long *)STEREO_INTERLEAVED);
    else if (info.channels == 1)
        ci->configure(DSP_SET_STEREO_MODE, (long *)STEREO_MONO);
    else
       return CODEC_ERROR;
    
    /* instantiate a decoder with our file reader */
    mpc_decoder_setup(&decoder, &reader);
    if (!mpc_decoder_initialize(&decoder, &info))
        return CODEC_ERROR;

    /* This is the decoding loop. */
    samplesdone = 0;
    while (status != 0) {
        if (ci->seek_time) {
            mpc_int64_t new_sample_offset = ci->seek_time*info.sample_freq/1000;
            if (mpc_decoder_seek_sample(&decoder, new_sample_offset)) {
                samplesdone = new_sample_offset;
                ci->set_elapsed(ci->seek_time);
            }
            ci->seek_complete();
        }
  
        if (ci->stop_codec || ci->reload_codec)
            break;

        status = mpc_decoder_decode(&decoder, sample_buffer, 0, 0);
        ci->yield();
        if (status == (unsigned)(-1)) { /* decode error */
            return CODEC_ERROR;
        } else {
            while (!ci->pcmbuf_insert((char *)sample_buffer,
                                      status*sizeof(MPC_SAMPLE_FORMAT)*2))
                ci->yield();
            samplesdone += status;
            ci->set_elapsed(samplesdone/(frequency/1000));
        }
    }
    if (ci->request_next_track())
        goto next_track;
    return CODEC_OK;
}

