#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int64_t start_us;
    int64_t paused_at_us;
    int64_t paused_us;
    bool paused;
} video_clock_t;

static inline int64_t video_clock_elapsed(const video_clock_t *clock, int64_t now)
{
    int64_t elapsed = (clock->paused ? clock->paused_at_us : now) - clock->start_us - clock->paused_us;
    return elapsed > 0 ? elapsed : 0;
}

static inline void video_clock_pause(video_clock_t *clock, bool paused, int64_t now)
{
    if (clock->paused == paused) { return; }
    if (paused) { clock->paused_at_us = now; }
    else { clock->paused_us += now - clock->paused_at_us; }
    clock->paused = paused;
}
