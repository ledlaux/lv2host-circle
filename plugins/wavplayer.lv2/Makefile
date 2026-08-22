include ../../../../Config.mk

# Object files
OBJS = ../../main.o ../../kernel.o ../../includes/lv2.o wavplayer.o sample.o

include $(CIRCLEHOME)/Rules.mk

# Compiler flags - silenced -Warray-bounds and -Wunused-variable
CFLAGS   += -O3 -Wno-parentheses -Wno-array-bounds -Wno-unused-variable
CXXFLAGS += -O3 -Wno-parentheses -ftree-vectorize

# Include paths relative to plugins/wavplayer.lv2
INCLUDE += $(CIRCLE_STDLIB_INCLUDES) \
           -I$(CIRCLEHOME)/lib/sound \
           -I../.. \
           -I../../includes \
           -I.. \
           -I.

# Static libraries
LIBS := $(CIRCLE_STDLIB_LIBS) $(CIRCLEHOME)/lib/sound/libsound.a

# Convert sample.wav to sample.o and rename symbol to match wavplayer.c
sample.o: sample.wav
	$(PREFIX)objcopy -I binary -O elf32-littlearm -B arm $< $@
	$(PREFIX)objcopy --redefine-sym _binary_sample_wav_start=_binary_plugins_wavplayer_lv2_sample_wav_start $@

# Custom clean target
clean: clean-custom

clean-custom:
	rm -f $(OBJS) $(DEPS) kernel8-32.elf kernel8-32.img kernel8-32.map kernel8-32.lst
	find ../.. -maxdepth 3 -type f \( -name "*.d" -o -name "*.o" \) -delete

-include $(DEPS)