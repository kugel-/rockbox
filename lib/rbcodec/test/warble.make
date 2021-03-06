#             __________               __   ___.
#   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
#   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
#   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
#   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
#                     \/            \/     \/    \/            \/
# $Id$
#



RBCODEC_DIR = $(ROOTDIR)/lib/rbcodec
RBCODEC_BLD = $(BUILDDIR)/lib/rbcodec

GCCOPTS += -D__PCTOOL__ $(TARGET) -DDEBUG -g -std=gnu99 `$(SDLCONFIG) --cflags` -DCODECDIR="\"$(CODECDIR)\""

SRC= $(call preprocess, $(ROOTDIR)/lib/rbcodec/test/SOURCES)

INCLUDES += -I$(ROOTDIR)/apps -I$(ROOTDIR)/apps/codecs -I$(ROOTDIR)/apps/codecs/lib \
			-I$(ROOTDIR)/apps/gui
INCLUDES += -I$(ROOTDIR)/firmware/export -I$(ROOTDIR)/firmware/include \
			-I$(ROOTDIR)/firmware/target/hosted \
			-I$(ROOTDIR)/firmware/target/hosted/sdl


.SECONDEXPANSION: # $$(OBJ) is not populated until after this

$(BUILDDIR)/$(BINARY): $(CODECS)

$(BUILDDIR)/$(BINARY): $$(OBJ) $$(CORE_LIBS)
	@echo LD $(BINARY)
	$(SILENT)$(HOSTCC) $(LDOPTS) -o $@ $(OBJ) \
		-L$(BUILDDIR)/lib $(call a2lnk, $(CORE_LIBS)) \
		$(LDOPTS) $(GLOBAL_LDOPTS)
