/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2002 Daniel Stenberg
 *
 * All files in this archive are subject to the GNU General Public License.
 * See the file COPYING in the source tree root for full license agreement.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "dir.h"
#include "file.h"
#include "lcd.h"
#include "button.h"
#include "kernel.h"
#include "tree.h"
#include "play.h"
#include "main_menu.h"
#include "sprintf.h"
#include "mpeg.h"
#include "playlist.h"

#ifdef HAVE_LCD_BITMAP
#include "icons.h"
#endif

#define MAX_FILES_IN_DIR 200
#define TREE_MAX_FILENAMELEN 128
#define MAX_DIR_LEVELS 10

struct entry {
  bool file; /* true if file, false if dir */
  char name[TREE_MAX_FILENAMELEN];
};

static struct entry dircache[MAX_FILES_IN_DIR];
static struct entry* dircacheptr[MAX_FILES_IN_DIR];
static int filesindir;

void browse_root(void)
{
  dirbrowse("/");
}


#ifdef HAVE_LCD_BITMAP

#define TREE_MAX_ON_SCREEN   7
#define TREE_MAX_LEN_DISPLAY 16 /* max length that fits on screen */
 
#define MARGIN_Y      8  /* Y pixel margin */
#define MARGIN_X      12 /* X pixel margin */
#define LINE_Y      0 /* Y position the entry-list starts at */
#define LINE_X      2 /* X position the entry-list starts at */
#define LINE_HEIGTH 8 /* pixels for each text line */
#define CURSOR_CHAR "-"

extern unsigned char bitmap_icons_6x8[LastIcon][6];

#else /* HAVE_LCD_BITMAP */

#define TREE_MAX_ON_SCREEN   2
#define TREE_MAX_LEN_DISPLAY 11 /* max length that fits on screen */
#define LINE_Y      0 /* Y position the entry-list starts at */
#define LINE_X      1 /* X position the entry-list starts at */

#ifdef HAVE_NEW_CHARCELL_LCD
#define CURSOR_CHAR "\x7e"
#else
#define CURSOR_CHAR "\x89"
#endif

#endif /* HAVE_LCD_BITMAP */

static int compare(const void* e1, const void* e2)
{
    return strncmp((*(struct entry**)e1)->name, (*(struct entry**)e2)->name,
                   TREE_MAX_FILENAMELEN);
}

static int showdir(char *path, int start)
{
    static char lastdir[256] = {0};

#ifdef HAVE_LCD_BITMAP
    int icon_type = 0;
#endif
    int i;

    /* new dir? cache it */
    if (strncmp(path,lastdir,sizeof(lastdir))) {
        DIR *dir = opendir(path);
        if(!dir)
            return -1; /* not a directory */
        memset(dircacheptr,0,sizeof(dircacheptr));
        for ( i=0; i<MAX_FILES_IN_DIR; i++ ) {
            struct dirent *entry = readdir(dir);
            if (!entry)
                break;
            if(entry->d_name[0] == '.') {
                /* skip names starting with a dot */
                i--;
                continue;
            }
            dircache[i].file = !(entry->attribute & ATTR_DIRECTORY);
            strncpy(dircache[i].name,entry->d_name,TREE_MAX_FILENAMELEN);
            dircache[i].name[TREE_MAX_FILENAMELEN-1]=0;
            dircacheptr[i] = &dircache[i];
        }
        filesindir = i;
        closedir(dir);
        strncpy(lastdir,path,sizeof(lastdir));
        lastdir[sizeof(lastdir)-1] = 0;
        qsort(dircacheptr,filesindir,sizeof(struct entry*),compare);
    }

#ifdef HAVE_NEW_CHARCELL_LCD
    lcd_double_height(false);
#endif
    lcd_clear_display();
#ifdef HAVE_LCD_BITMAP
    lcd_putsxy(0,0, "[Browse]",0);
    lcd_update();
#endif

    for ( i=start; i < start+TREE_MAX_ON_SCREEN; i++ ) {
        int len;

        if ( i >= filesindir )
            break;

        len = strlen(dircacheptr[i]->name);

#ifdef HAVE_LCD_BITMAP
        if ( dircacheptr[i]->file )
            icon_type=File;
        else
            icon_type=Folder;
        lcd_bitmap(bitmap_icons_6x8[icon_type], 
                   6, MARGIN_Y+(i-start)*LINE_HEIGTH, 6, 8, true);
#endif

        lcd_puts(LINE_X, LINE_Y+i-start, dircacheptr[i]->name);
    }

    return filesindir;
}

static int numentries=0;
static int dircursor=0;
static int start=0;
static int dirpos[MAX_DIR_LEVELS];
static int cursorpos[MAX_DIR_LEVELS];
static int dirlevel=0;
static int playing = 0;
static char currdir[255];

/* QUICK HACK! this should be handled by the playlist code later */
char* peek_next_track(int type)
{
    static char buf[256];

    /* next-song only works when playing */
    if (!playing)
        return NULL;

    switch(playing) {
    default:
    case 1:
      /* play-full-dir mode */
      
      /* get next track in dir */
      while (dircursor + start + 1 < numentries ) {
          if(dircursor+1 < TREE_MAX_ON_SCREEN)
            dircursor++;
          else
            start++;
          if ( dircacheptr[dircursor+start]->file &&
               dircacheptr[dircursor+start]->name[strlen(dircacheptr[dircursor+start]->name)-1] == '3') {
              snprintf(buf,sizeof buf,"%s/%s",
                       currdir, dircacheptr[dircursor+start]->name );
              lcd_clear_display();
              lcd_puts(0,0,"<Playing>");
              lcd_puts(0,1,"<all files>");
              return buf;
          }
      }
      break;
      
    case 2:
      /* playlist mode */
      return playlist_next(type);
    }

    return NULL;
}

bool dirbrowse(char *root)
{
    char buf[255];
    int i;
    lcd_clear_display();

#ifdef HAVE_LCD_BITMAP
    lcd_putsxy(0,0, "[Browse]",0);
    lcd_setmargins(0,MARGIN_Y);
    lcd_setfont(0);
#endif
    memcpy(currdir,root,sizeof(currdir));

    numentries = showdir(root, start);

    if (numentries == -1) 
        return -1;  /* root is not a directory */

    lcd_puts(0, dircursor, CURSOR_CHAR);
    lcd_puts_scroll(LINE_X, LINE_Y+dircursor,
                    dircacheptr[start+dircursor]->name);
#ifdef HAVE_LCD_BITMAP
    lcd_update();
#endif

    while(1) {
        switch(button_get(true)) {
#if defined(SIMULATOR) && defined(HAVE_RECODER_KEYPAD)
            case BUTTON_OFF:
                return false;
#endif

#ifdef HAVE_RECORDER_KEYPAD
            case BUTTON_LEFT:
#else
            case BUTTON_STOP:
#endif
                i=strlen(currdir);
                if (i>1) {
                    while (currdir[i-1]!='/')
                        i--;
                    strcpy(buf,&currdir[i]);
                    if (i==1)
                        currdir[i]=0;
                    else
                        currdir[i-1]=0;

                    dirlevel--;
                    if ( dirlevel < MAX_DIR_LEVELS ) {
                        start = dirpos[dirlevel];
                        dircursor = cursorpos[dirlevel];
                    }
                    else
                        start = dircursor = 0;
                    lcd_stop_scroll();
                    numentries = showdir(currdir, start);
                    lcd_puts(0, LINE_Y+dircursor, CURSOR_CHAR);
                }
                else
                    mpeg_stop();

                break;

#ifdef HAVE_RECORDER_KEYPAD
            case BUTTON_RIGHT:
#endif
            case BUTTON_PLAY:
                if ((currdir[0]=='/') && (currdir[1]==0)) {
                    snprintf(buf,sizeof(buf),"%s%s",currdir,
                             dircacheptr[dircursor+start]->name);
                } else {
                    snprintf(buf,sizeof(buf),"%s/%s",currdir,
                             dircacheptr[dircursor+start]->name);
                }

                if (!dircacheptr[dircursor+start]->file) {
                    memcpy(currdir,buf,sizeof(currdir));
                    if ( dirlevel < MAX_DIR_LEVELS ) {
                      dirpos[dirlevel] = start;
                      cursorpos[dirlevel] = dircursor;
                    }
                    dirlevel++;
                    dircursor=0;
                    start=0;
                } else {
                    int len=strlen(dircacheptr[dircursor+start]->name);
                    lcd_stop_scroll();
                    if((len > 4) &&
                       !strcmp(&dircacheptr[dircursor+start]->name[len-4],
                               ".m3u")) {
                        playing = 2;
                        play_list(currdir, dircacheptr[dircursor+start]->name);
                    }
                    
                    else {

                        playing = 1;
                        playtune(buf);
                        playing = 0;
#ifdef HAVE_LCD_BITMAP
                        lcd_setmargins(0, MARGIN_Y);
                        lcd_setfont(0);
#endif
                    }
                }

                numentries = showdir(currdir, start);  
                lcd_puts(0, LINE_Y+dircursor, CURSOR_CHAR);
                break;
                
#ifdef HAVE_RECORDER_KEYPAD
            case BUTTON_UP:
#else
            case BUTTON_LEFT:
#endif
                if(dircursor) {
                    lcd_puts(0, LINE_Y+dircursor, " ");
                    dircursor--;
                    lcd_puts(0, LINE_Y+dircursor, CURSOR_CHAR);
                    lcd_update();
                }
                else {
                    if (start) {
                        start--;
                        numentries = showdir(currdir, start);
                        lcd_puts(0, LINE_Y+dircursor, CURSOR_CHAR);
                    }
                }
                break;

#ifdef HAVE_RECORDER_KEYPAD
            case BUTTON_DOWN:
#else
            case BUTTON_RIGHT:
#endif
                if (dircursor + start + 1 < numentries ) {
                    if(dircursor+1 < TREE_MAX_ON_SCREEN) {
                        lcd_puts(0, LINE_Y+dircursor, " ");
                        dircursor++;
                        lcd_puts(0, LINE_Y+dircursor, CURSOR_CHAR);
                    } 
                    else {
                        start++;
                        numentries = showdir(currdir, start);
                        lcd_puts(0, LINE_Y+dircursor, CURSOR_CHAR);
                    }
                }
                break;

#ifdef HAVE_RECORDER_KEYPAD
            case BUTTON_F1:
            case BUTTON_F2:
            case BUTTON_F3:
#else
            case BUTTON_MENU:
#endif
                lcd_stop_scroll();
                main_menu();

                /* restore display */
                /* TODO: this is just a copy from BUTTON_STOP, fix it */
                lcd_clear_display();
#ifdef HAVE_LCD_BITMAP
                lcd_putsxy(0,0, "[Browse]",0);
                lcd_setmargins(0,MARGIN_Y);
                lcd_setfont(0);
#endif
                numentries = showdir(currdir, start);
                lcd_puts(0, LINE_Y+dircursor, CURSOR_CHAR);

                break;
        }

        lcd_stop_scroll();
        lcd_puts_scroll(LINE_X, LINE_Y+dircursor,
                        dircacheptr[start+dircursor]->name);

        lcd_update();
    }

    return false;
}
