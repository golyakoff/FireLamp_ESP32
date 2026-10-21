// Host test of schedule.c: power on/off, dawn ramp (incl. midnight, DST, cancel), hourly brightness
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "schedule.h"

static time_t local(int y, int mo, int d, int h, int mi, int s)
{
    struct tm t = { .tm_year = y - 1900, .tm_mon = mo - 1, .tm_mday = d, .tm_hour = h, .tm_min = mi, .tm_sec = s, .tm_isdst = -1 };
    return mktime(&t);
}

typedef struct { int on, off, dawn_start, dawn_end, bright; uint8_t last_b, first_b; char dawn_effect[17]; } counts_t;

// run evaluation every second from `from` to `to`, simulating the lamp state
static counts_t run(const schedule_t *s, schedule_state_t *st, time_t from, time_t to, bool *playing)
{
    counts_t c = { 0 };
    schedule_action_t a[SCHEDULE_MAX_ACTIONS];
    for (time_t now = from; now < to; now++)
    {
        size_t n = schedule_evaluate(s, st, now, *playing, a);
        assert(n <= SCHEDULE_MAX_ACTIONS);
        for (size_t i = 0; i < n; i++)
            switch (a[i].type)
            {
                case SCHEDULE_ACTION_POWER_ON: c.on++; *playing = true; break;
                case SCHEDULE_ACTION_POWER_OFF: c.off++; *playing = false; break;
                case SCHEDULE_ACTION_DAWN_START: c.dawn_start++; *playing = true; c.first_b = a[i].brightness; c.last_b = a[i].brightness; strcpy(c.dawn_effect, a[i].effect); break;
                case SCHEDULE_ACTION_BRIGHTNESS: c.bright++; assert(a[i].brightness >= c.last_b || !st->dawn_active); c.last_b = a[i].brightness; break;
                case SCHEDULE_ACTION_DAWN_END: c.dawn_end++; c.last_b = a[i].brightness; break;
            }
    }
    return c;
}

int main(void)
{
    setenv("TZ", "MSK-3", 1);
    tzset();

    schedule_t s;
    schedule_state_t st;
    bool playing;
    counts_t c;

    schedule_defaults(&s);
    assert(schedule_validate(&s));

    // nothing enabled -> no actions over a day
    schedule_state_init(&st); playing = false;
    c = run(&s, &st, local(2026, 10, 21, 0, 0, 0), local(2026, 10, 22, 0, 0, 0), &playing);
    assert(!c.on && !c.off && !c.dawn_start && !c.bright);

    // power on 07:00 / off 23:00 every day: exactly once each per day
    s.power_on.enabled = s.power_off.enabled = 1;
    schedule_state_init(&st); playing = false;
    c = run(&s, &st, local(2026, 10, 21, 0, 0, 0), local(2026, 10, 23, 0, 0, 0), &playing);
    assert(c.on == 2 && c.off == 2 && !playing);

    // days mask: only Monday (2026-10-19 is a Monday) for a week
    s.power_on.days = 0x01; s.power_off.enabled = 0;
    schedule_state_init(&st); playing = false;
    c = run(&s, &st, local(2026, 10, 19, 0, 0, 0), local(2026, 10, 26, 0, 0, 0), &playing);
    assert(c.on == 1);
    s.power_on.enabled = 0;

    // time jump forward over the event: missed events are not replayed
    schedule_state_init(&st); playing = false;
    s.power_on.enabled = 1; s.power_on.days = 0x7f;
    c = run(&s, &st, local(2026, 10, 21, 6, 0, 0), local(2026, 10, 21, 6, 59, 0), &playing);
    c = run(&s, &st, local(2026, 10, 21, 7, 5, 0), local(2026, 10, 21, 8, 0, 0), &playing);
    assert(c.on == 0);
    s.power_on.enabled = 0;

    // dawn 07:00, 30 min, Wednesday 2026-10-21 (working day)
    s.dawn.at.enabled = 1; strcpy(s.dawn.effect, "fire"); s.dawn.brightness = 200;
    schedule_state_init(&st); playing = false;
    c = run(&s, &st, local(2026, 10, 21, 6, 0, 0), local(2026, 10, 21, 6, 30, 0), &playing);
    assert(!c.dawn_start && !playing);
    c = run(&s, &st, local(2026, 10, 21, 6, 30, 0), local(2026, 10, 21, 7, 0, 0), &playing);
    assert(c.dawn_start == 1 && c.first_b == 1 && !strcmp(c.dawn_effect, "fire") && playing);
    assert(c.last_b >= 195 && c.last_b < 200);
    printf("dawn 30 min to 200: %d brightness steps\n", c.bright);
    c = run(&s, &st, local(2026, 10, 21, 7, 0, 0), local(2026, 10, 21, 9, 0, 0), &playing);
    assert(c.dawn_end == 1 && c.last_b == 200 && !c.dawn_start && !c.bright);

    // no dawn on Saturday 2026-10-24
    schedule_state_init(&st); playing = false;
    c = run(&s, &st, local(2026, 10, 24, 6, 0, 0), local(2026, 10, 24, 8, 0, 0), &playing);
    assert(!c.dawn_start && !playing);

    // boot in the middle of the dawn: starts from the current ramp value
    schedule_state_init(&st); playing = false;
    c = run(&s, &st, local(2026, 10, 21, 6, 45, 0), local(2026, 10, 21, 6, 45, 1), &playing);
    assert(c.dawn_start == 1 && c.first_b >= 99 && c.first_b <= 101);

    // cancel: no more brightness changes and no DAWN_END, next day works again
    schedule_state_init(&st); playing = false;
    c = run(&s, &st, local(2026, 10, 21, 6, 30, 0), local(2026, 10, 21, 6, 40, 0), &playing);
    assert(c.dawn_start == 1);
    schedule_cancel_dawn(&st);
    c = run(&s, &st, local(2026, 10, 21, 6, 40, 0), local(2026, 10, 21, 8, 0, 0), &playing);
    assert(!c.dawn_start && !c.bright && !c.dawn_end);
    c = run(&s, &st, local(2026, 10, 22, 6, 0, 0), local(2026, 10, 22, 7, 30, 0), &playing);
    assert(c.dawn_start == 1 && c.dawn_end == 1);

    // dawn crossing midnight: alarm 00:10 Thursday, 30 min ramp starts Wednesday 23:40
    s.dawn.at.hour = 0; s.dawn.at.minute = 10;
    schedule_state_init(&st); playing = false;
    c = run(&s, &st, local(2026, 10, 21, 23, 0, 0), local(2026, 10, 22, 1, 0, 0), &playing);
    assert(c.dawn_start == 1 && c.dawn_end == 1);
    s.dawn.at.hour = 7; s.dawn.at.minute = 0;
    s.dawn.at.enabled = 0;

    // hourly brightness: applied when turned on and on every hour change, not when off
    s.hourly.enabled = 1;
    for (int h = 0; h < 24; h++) s.hourly.values[h] = 10 + h;
    schedule_state_init(&st); playing = false;
    c = run(&s, &st, local(2026, 10, 21, 10, 0, 0), local(2026, 10, 21, 12, 0, 0), &playing);
    assert(!c.bright);
    playing = true;
    c = run(&s, &st, local(2026, 10, 21, 12, 30, 0), local(2026, 10, 21, 15, 30, 0), &playing);
    assert(c.bright == 4 && c.last_b == 10 + 15); // 12 (on), 13, 14, 15

    // hourly brightness must not jump in during or right after the dawn
    s.dawn.at.enabled = 1;
    schedule_state_init(&st); playing = false;
    c = run(&s, &st, local(2026, 10, 21, 6, 30, 0), local(2026, 10, 21, 7, 59, 0), &playing);
    assert(c.dawn_end == 1 && c.last_b == 200);
    c = run(&s, &st, local(2026, 10, 21, 7, 59, 0), local(2026, 10, 21, 8, 0, 1), &playing);
    assert(c.bright == 1 && c.last_b == 10 + 8);
    s.dawn.at.enabled = 0; s.hourly.enabled = 0;

    // DST: CET, 2026-03-29 02:00 -> 03:00. Dawn at 07:00 local is still 07:00 local.
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    s.dawn.at.enabled = 1; s.dawn.at.days = 0x7f;
    schedule_state_init(&st); playing = false;
    time_t start = local(2026, 3, 29, 6, 30, 0);
    c = run(&s, &st, local(2026, 3, 29, 0, 0, 0), local(2026, 3, 29, 6, 30, 0), &playing);
    assert(!c.dawn_start);
    c = run(&s, &st, start, start + 1, &playing);
    assert(c.dawn_start == 1);

    // validation
    schedule_defaults(&s);
    s.dawn.duration = 0; assert(!schedule_validate(&s));
    schedule_defaults(&s); s.power_on.hour = 24; assert(!schedule_validate(&s));
    schedule_defaults(&s); s.hourly.values[3] = 0; assert(!schedule_validate(&s));
    schedule_defaults(&s); strcpy(s.dawn.effect, "Fire"); assert(!schedule_validate(&s));
    schedule_defaults(&s); s.dawn.effect[0] = 0; assert(!schedule_validate(&s));
    schedule_defaults(&s); assert(!strcmp(s.dawn.effect, "lamp"));

    printf("schedule: all tests passed\n");
    return 0;
}
