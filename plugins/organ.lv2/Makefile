resolve ../../../../Config.mk

OBJS = ../../main.o ../../kernel.o ../../includes/lv2.o organ.o

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
	rm -f $(OBJS) $(DEPS)
	find ../.. -maxdepth 3 -type f \( -name "*.d" -o -name "*.o" \) -delete

-include $(DEPS)
