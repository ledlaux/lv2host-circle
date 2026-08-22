include ../../../../Config.mk

OBJS = ../../main.o ../../kernel.o ../../includes/lv2.o piano.o

include $(CIRCLEHOME)/Rules.mk

CFLAGS   += -O3 -Wno-parentheses
CXXFLAGS += -O3 -Wno-parentheses

INCLUDE += $(CIRCLE_STDLIB_INCLUDES) \
           -I$(CIRCLEHOME)/lib/sound \
           -I../.. \
           -I../../includes \
           -I.. \
           -I.

LIBS := $(CIRCLE_STDLIB_LIBS) $(CIRCLEHOME)/lib/sound/libsound.a

clean: clean-custom

clean-custom:
	rm -f $(OBJS) $(DEPS) kernel8-32.elf kernel8-32.img kernel8-32.map kernel8-32.lst
	find ../.. -maxdepth 3 -type f \( -name "*.d" -o -name "*.o" \) -delete

-include $(DEPS)