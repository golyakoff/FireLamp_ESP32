# Host unit tests for hardware independent logic (storage, input gestures).
# Run: make -C test/host

CC ?= gcc
MAIN := ../../main
CFLAGS := -Wall -Wno-unused-function -Wno-unused-variable -std=gnu17 -D_GNU_SOURCE \
	-include stub/host_common.h -Istub -I$(MAIN)

INPUT1 := -DCONFIG_FL_INPUT_ONE_BUTTON=1 -DCONFIG_FL_BUTTON_MAIN_GPIO=4 \
	-DCONFIG_FL_BUTTON_LEVEL=1 -DCONFIG_FL_BUTTON_PULLUPDOWN=1
INPUT4 := -DCONFIG_FL_INPUT_FOUR_BUTTONS=1 -DCONFIG_FL_BUTTON_MAIN_GPIO=25 \
	-DCONFIG_FL_BUTTON_UP_GPIO=26 -DCONFIG_FL_BUTTON_DOWN_GPIO=27 -DCONFIG_FL_BUTTON_RESET_GPIO=0 \
	-DCONFIG_FL_BUTTON_LEVEL=1 -DCONFIG_FL_BUTTON_RESET_LEVEL=0 -DCONFIG_FL_BUTTON_PULLUPDOWN=1

TESTS := build/test_storage build/test_input build/test_input4 build/test_schedule build/test_demo

.PHONY: all clean
all: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

build:
	mkdir -p build

build/test_storage: test_storage.c $(MAIN)/storage.c $(MAIN)/storage.h | build
	$(CC) $(CFLAGS) -DCONFIG_FL_STORAGE_EEPROM=1 -o $@ test_storage.c $(MAIN)/storage.c

build/test_input: test_input.c $(MAIN)/input.c $(MAIN)/input.h | build
	$(CC) $(CFLAGS) $(INPUT1) -o $@ test_input.c

build/test_input4: test_input4.c $(MAIN)/input.c $(MAIN)/input.h | build
	$(CC) $(CFLAGS) $(INPUT4) -o $@ test_input4.c

# schedule.c is plain C without ESP-IDF dependencies
build/test_schedule: test_schedule.c $(MAIN)/schedule.c $(MAIN)/schedule.h | build
	$(CC) -Wall -std=gnu17 -I$(MAIN) -o $@ test_schedule.c $(MAIN)/schedule.c

build/test_demo: test_demo.c $(MAIN)/demo_core.c $(MAIN)/demo_core.h | build
	$(CC) -Wall -std=gnu17 -I$(MAIN) -o $@ test_demo.c $(MAIN)/demo_core.c

clean:
	rm -rf build
