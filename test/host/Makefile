# Host unit tests for hardware independent logic (storage, input gestures).
# Run: make -C test/host

CC ?= gcc
MAIN := ../../main
CFLAGS := -Wall -Wno-unused-function -Wno-unused-variable -std=gnu17 -D_GNU_SOURCE \
	-include stub/host_common.h -Istub -I$(MAIN)

# button pins and levels (the control elements are chosen at runtime)
INPUT_CFG := -DCONFIG_FL_BUTTON_MAIN_GPIO=25 -DCONFIG_FL_BUTTON_UP_GPIO=26 \
	-DCONFIG_FL_BUTTON_DOWN_GPIO=27 -DCONFIG_FL_BUTTON_RESET_GPIO=0 \
	-DCONFIG_FL_BUTTON_RESET_LEVEL=0

TESTS := build/test_storage build/test_input build/test_input4 build/test_input_low build/test_schedule build/test_demo

.PHONY: all clean
all: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

build:
	mkdir -p build

build/test_storage: test_storage.c $(MAIN)/storage.c $(MAIN)/storage.h | build
	$(CC) $(CFLAGS) -o $@ test_storage.c $(MAIN)/storage.c

build/test_input: test_input.c $(MAIN)/input.c $(MAIN)/input.h | build
	$(CC) $(CFLAGS) $(INPUT_CFG) -o $@ test_input.c

build/test_input_low: test_input_low.c $(MAIN)/input.c $(MAIN)/input.h | build
	$(CC) $(CFLAGS) $(INPUT_CFG) -o $@ test_input_low.c

build/test_input4: test_input4.c $(MAIN)/input.c $(MAIN)/input.h | build
	$(CC) $(CFLAGS) $(INPUT_CFG) -o $@ test_input4.c

# schedule.c is plain C without ESP-IDF dependencies
build/test_schedule: test_schedule.c $(MAIN)/schedule.c $(MAIN)/schedule.h | build
	$(CC) -Wall -std=gnu17 -I$(MAIN) -o $@ test_schedule.c $(MAIN)/schedule.c

build/test_demo: test_demo.c $(MAIN)/demo_core.c $(MAIN)/demo_core.h | build
	$(CC) -Wall -std=gnu17 -I$(MAIN) -o $@ test_demo.c $(MAIN)/demo_core.c

clean:
	rm -rf build
