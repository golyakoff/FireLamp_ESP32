// Host test of input.c gesture engine (one button mode)
#include <assert.h>
#include "input.h"
#include "bus.h"

int fake_level = 0;
int64_t fake_now_us = 0;

typedef struct { event_type_t type; input_event_t e; int64_t at_ms; } rec_t;
static rec_t log_[1000];
static int n = 0;

esp_err_t bus_send_event(event_type_t type, void *data, size_t size)
{
    log_[n].type = type; memcpy(&log_[n].e, data, sizeof(input_event_t)); log_[n].at_ms = fake_now_us / 1000; n++;
    return ESP_OK;
}

// pull engine internals in
#include "input.c"

static void run_ms(int ms) { for (int i = 0; i < ms / INPUT_POLL_MS; i++) { fake_now_us += INPUT_POLL_MS * 1000; process(INPUT_BTN_MAIN, fake_now_us); } }
static void press(int ms) { fake_level = 1; run_ms(ms); fake_level = 0; }
static void reset(void) { run_ms(2000); n = 0; }

static const char *name(event_type_t t)
{
    return t == EVENT_BUTTON_CLICKED ? "CLICK" : t == EVENT_BUTTON_HOLD_START ? "HOLD_START" : t == EVENT_BUTTON_HOLD ? "HOLD" : t == EVENT_BUTTON_HOLD_END ? "HOLD_END" : "?";
}

int main(void)
{
    // Main button pins (ESP32 reference pinout, see INPUT_CFG in the Makefile): candidates
    // without the matrix, I2C and Up/Down/Reset pins; 0 or a pin not in the list - the default
    size_t pins_count;
    const uint8_t *pins = input_main_gpios(&pins_count);
    static const uint8_t expected[] = { 4, 13, 14, 18, 19, 23, 25, 32, 33 };
    assert(pins_count == sizeof(expected) && !memcmp(pins, expected, sizeof(expected)));
    assert(input_main_gpio_allowed(25) && input_main_gpio_allowed(4) && input_main_gpio_allowed(33));
    assert(!input_main_gpio_allowed(0) && !input_main_gpio_allowed(5) && !input_main_gpio_allowed(26)
           && !input_main_gpio_allowed(34) && !input_main_gpio_allowed(12));
    assert(input_main_gpio(0) == 25 && input_main_gpio(13) == 13 && input_main_gpio(5) == 25);
    input_init(INPUT_MODE_ONE_BUTTON, 1, 13);
    assert(buttons[INPUT_BTN_MAIN].gpio == 13);

    input_init(INPUT_MODE_ONE_BUTTON, 1, 0);
    assert(buttons[INPUT_BTN_MAIN].gpio == 25);

    // single click
    reset(); press(100); run_ms(500);
    assert(n == 1 && log_[0].type == EVENT_BUTTON_CLICKED && log_[0].e.clicks == 1);

    // double click
    reset(); press(100); run_ms(150); press(100); run_ms(500);
    assert(n == 1 && log_[0].e.clicks == 2);

    // triple click is reported after the multi-click timeout
    reset(); press(80); run_ms(100); press(80); run_ms(100); press(80); run_ms(30);
    assert(n == 0);
    run_ms(400);
    assert(n == 1 && log_[0].e.clicks == 3);

    // 5 clicks (pairing gesture) are reported immediately on the fifth release
    reset();
    for (int i = 0; i < 4; i++) { press(80); run_ms(100); }
    press(80); run_ms(30);
    assert(n == 1 && log_[0].e.clicks == 5);

    // two clicks separated by more than the timeout are two single clicks
    reset(); press(100); run_ms(600); press(100); run_ms(600);
    assert(n == 2 && log_[0].e.clicks == 1 && log_[1].e.clicks == 1);

    // bounce shorter than debounce time is ignored
    reset(); fake_level = 1; run_ms(10); fake_level = 0; run_ms(10); fake_level = 1; run_ms(10); fake_level = 0; run_ms(600);
    assert(n == 0);

    // contact bounce during a real press is filtered: still one click
    reset(); fake_level = 1; run_ms(10); fake_level = 0; run_ms(10); fake_level = 1; run_ms(200); fake_level = 0; run_ms(10); fake_level = 1; run_ms(10); fake_level = 0; run_ms(600);
    assert(n == 1 && log_[0].e.clicks == 1);

    // hold for 2 s: HOLD_START once, repeats every 50 ms, HOLD_END, no click
    reset(); press(2000); run_ms(600);
    assert(log_[0].type == EVENT_BUTTON_HOLD_START);
    assert(log_[n - 1].type == EVENT_BUTTON_HOLD_END);
    int holds = 0; for (int i = 0; i < n; i++) { assert(log_[i].type != EVENT_BUTTON_CLICKED); if (log_[i].type == EVENT_BUTTON_HOLD) holds++; }
    printf("hold 2 s: start at %lld ms after press, %d repeats, end hold_ms %lu\n",
        (long long)(log_[0].at_ms - (log_[0].at_ms - log_[0].e.hold_ms)), holds, (unsigned long)log_[n-1].e.hold_ms);
    assert(holds >= 25 && holds <= 30);
    assert(log_[n - 1].e.hold_ms >= 1990 && log_[n - 1].e.hold_ms <= 2040);

    // click then hold: hold reports the preceding click
    reset(); press(100); run_ms(150); press(1000); run_ms(600);
    assert(log_[0].type == EVENT_BUTTON_HOLD_START && log_[0].e.clicks == 1);
    for (int i = 0; i < n; i++) assert(log_[i].type != EVENT_BUTTON_CLICKED);

    // after a hold the next click is a fresh single click
    reset(); press(1000); run_ms(600); n = 0; press(100); run_ms(600);
    assert(n == 1 && log_[0].e.clicks == 1);

    // 10 s hold reaches the factory reset threshold
    reset(); press(10500);
    uint32_t maxhold = 0; for (int i = 0; i < n; i++) if (log_[i].e.hold_ms > maxhold) maxhold = log_[i].e.hold_ms;
    assert(maxhold >= 10000);

    printf("input: all tests passed\n");
    (void)name;
    return 0;
}
