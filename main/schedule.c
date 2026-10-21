#include "schedule.h"
#include <string.h>

static inline bool day_enabled(uint8_t days, const struct tm *t)
{
    // tm_wday: 0 - Sunday
    return days & (1 << ((t->tm_wday + 6) % 7));
}

static inline bool time_matches(const schedule_time_t *st, const struct tm *t)
{
    return st->enabled && st->hour == t->tm_hour && st->minute == t->tm_min && day_enabled(st->days, t);
}

// Moment of the dawn alarm whose ramp covers `now`, 0 if there is none
static time_t find_dawn(const schedule_t *s, time_t now)
{
    if (!s->dawn.at.enabled || !s->dawn.at.days)
        return 0;

    time_t ramp = s->dawn.duration * 60;
    struct tm t;
    localtime_r(&now, &t);

    // the alarm is today or tomorrow (ramp may start before midnight)
    for (int day = 0; day < 2; day++)
    {
        struct tm a = t;
        a.tm_mday += day;
        a.tm_hour = s->dawn.at.hour;
        a.tm_min = s->dawn.at.minute;
        a.tm_sec = 0;
        a.tm_isdst = -1;
        time_t alarm = mktime(&a); // normalizes tm_mday and tm_wday
        if (alarm == (time_t)-1 || !day_enabled(s->dawn.at.days, &a))
            continue;
        if (now >= alarm - ramp && now < alarm)
            return alarm;
    }
    return 0;
}

void schedule_defaults(schedule_t *s)
{
    memset(s, 0, sizeof(schedule_t));
    s->power_on.days = 0x7f;
    s->power_on.hour = 7;
    s->power_off.days = 0x7f;
    s->power_off.hour = 23;
    s->dawn.at.days = 0x1f; // working days
    s->dawn.at.hour = 7;
    s->dawn.duration = 30;
    s->dawn.effect = 0;
    s->dawn.brightness = 255;
    for (size_t h = 0; h < 24; h++)
        s->hourly.values[h] = h >= 8 && h < 22 ? 255 : 32;
}

static bool time_valid(const schedule_time_t *t)
{
    return t->hour < 24 && t->minute < 60 && t->days < 0x80;
}

bool schedule_validate(const schedule_t *s)
{
    if (!time_valid(&s->power_on) || !time_valid(&s->power_off) || !time_valid(&s->dawn.at))
        return false;
    if (s->dawn.duration < SCHEDULE_DAWN_MIN_DURATION || s->dawn.duration > SCHEDULE_DAWN_MAX_DURATION)
        return false;
    if (!s->dawn.brightness)
        return false;
    for (size_t h = 0; h < 24; h++)
        if (!s->hourly.values[h])
            return false;
    return true;
}

void schedule_state_init(schedule_state_t *st)
{
    memset(st, 0, sizeof(schedule_state_t));
    st->last_minute = -1;
    st->hourly_hour = -1;
}

void schedule_cancel_dawn(schedule_state_t *st)
{
    if (st->dawn_active)
    {
        st->dawn_active = false;
        st->dawn_cancelled = true;
    }
}

size_t schedule_evaluate(const schedule_t *s, schedule_state_t *st, time_t now, bool playing,
        schedule_action_t *actions)
{
    size_t n = 0;
    struct tm t;
    localtime_r(&now, &t);

    // power on/off: once per matching minute
    int64_t minute = now / 60;
    if (minute != st->last_minute)
    {
        st->last_minute = minute;
        if (time_matches(&s->power_off, &t))
        {
            actions[n++] = (schedule_action_t) { .type = SCHEDULE_ACTION_POWER_OFF };
            playing = false;
        }
        else if (time_matches(&s->power_on, &t))
        {
            actions[n++] = (schedule_action_t) { .type = SCHEDULE_ACTION_POWER_ON };
            playing = true;
        }
    }

    // dawn
    time_t alarm = find_dawn(s, now);
    if (alarm)
    {
        if (alarm != st->dawn_alarm)
        {
            st->dawn_alarm = alarm;
            st->dawn_active = false;
            st->dawn_cancelled = false;
        }
        if (!st->dawn_cancelled)
        {
            time_t ramp = s->dawn.duration * 60;
            time_t elapsed = now - (alarm - ramp);
            uint8_t b = 1 + (uint8_t)((s->dawn.brightness - 1) * elapsed / ramp);
            if (!st->dawn_active)
            {
                st->dawn_active = true;
                st->dawn_brightness = b;
                actions[n++] = (schedule_action_t) {
                    .type = SCHEDULE_ACTION_DAWN_START,
                    .brightness = b,
                    .effect = s->dawn.effect,
                };
                playing = true;
            }
            else if (b != st->dawn_brightness)
            {
                st->dawn_brightness = b;
                actions[n++] = (schedule_action_t) { .type = SCHEDULE_ACTION_BRIGHTNESS, .brightness = b };
            }
            // the hourly brightness waits for the next hour after the dawn ends or is cancelled
            st->hourly_hour = t.tm_hour;
        }
    }
    else if (st->dawn_active)
    {
        st->dawn_active = false;
        if (now >= st->dawn_alarm)
        {
            // ramp finished: keep the final brightness
            actions[n++] = (schedule_action_t) { .type = SCHEDULE_ACTION_DAWN_END, .brightness = s->dawn.brightness };
            // the alarm is usually at a round hour: keep the dawn result until the next hour
            st->hourly_hour = t.tm_hour;
        }
    }

    // hourly brightness: on every hour change and when the lamp turns on
    if (!playing)
        st->hourly_hour = -1;
    else if (s->hourly.enabled && !st->dawn_active && st->hourly_hour != t.tm_hour)
    {
        st->hourly_hour = t.tm_hour;
        actions[n++] = (schedule_action_t) { .type = SCHEDULE_ACTION_BRIGHTNESS, .brightness = s->hourly.values[t.tm_hour] };
    }

    return n;
}
