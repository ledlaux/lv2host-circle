include ../../../../Config.mk

# Added ../../includes/lv2.o to resolve undefined references
OBJS = ../../main.o ../../kernel.o ../../includes/lv2.o organ.o

include $(CIRCLEHOME)/Rules.mk

# Compiler flags with -O3 optimization
CFLAGS   += -O3 -Wno-parentheses
CXXFLAGS += -O3 -Wno-parentheses

# Include paths relative to plugins/organ.lv2
INCLUDE += $(CIRCLE_STDLIB_INCLUDES) \
           -I$(CIRCLEHOME)/lib/sound \
           -I../.. \
           -I../../includes \
           -I.. \
           -I.

# Static libraries
LIBS := $(CIRCLE_STDLIB_LIBS) $(CIRCLEHOME)/lib/sound/libsound.a

# Custom clean target
clean: clean-custom

clean-custom:
	rm -f $(OBJS) $(DEPS)
	find ../.. -maxdepth 3 -type f \( -name "*.d" -o -name "*.o" \) -delete

-include $(DEPS)