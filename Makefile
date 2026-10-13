# TouchWeather for the Synthux Touch 2 (Daisy Seed)
#
#   make libs          build libDaisy once
#   make               build the firmware
#   make program-dfu   flash it over USB (Seed in DFU mode: hold BOOT, tap RESET)
#   make DEBUG=1       USB-serial log: every pad as it is touched, the panel, the CPU
#   make test          the host tests (no hardware needed)

# DEBUG=1 turns on the serial log. It is how to find out which electrode is
# which pad (see src/ui/layout.h) and how hard the CPU is working.
ifeq ($(DEBUG), 1)
C_DEFS += -DTW_DEBUG=1
endif

TARGET = touchweather

CPP_STANDARD = -std=c++17

LIBDAISY_DIR = libDaisy

C_INCLUDES = -Isrc/
C_USR_FLAGS = -ffast-math -funroll-loops

CPP_SOURCES = \
	src/main.cpp \
	src/app.cpp \
	src/hw/touch2.cpp \
	src/ui/weather.ui.cpp

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

libs:
	cd $(LIBDAISY_DIR) && $(MAKE)

clean-libs:
	cd $(LIBDAISY_DIR) && $(MAKE) clean

test:
	$(MAKE) -C tests
