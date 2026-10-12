#include "video_clock.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    video_clock_t c = { .start_us = 1000000 };
    assert(video_clock_elapsed(&c, 0) == 0);
    assert(video_clock_elapsed(&c, 2000000) == 1000000);
    video_clock_pause(&c, true, 2000000);
    assert(video_clock_elapsed(&c, 9000000) == 1000000);
    video_clock_pause(&c, true, 6000000);
    video_clock_pause(&c, false, 9000000);
    assert(video_clock_elapsed(&c, 10000000) == 2000000);
    video_clock_pause(&c, false, 11000000);
    assert(video_clock_elapsed(&c, 11000000) == 3000000);
    video_clock_pause(&c, true, 11000000);
    video_clock_pause(&c, false, 14000000);
    assert(video_clock_elapsed(&c, 15000000) == 4000000);
    c = (video_clock_t){ .start_us = INT64_C(9000000000) };
    assert(video_clock_elapsed(&c, INT64_C(9000000000) + INT64_C(219000000)) == INT64_C(219000000));
    puts("Pause, resume, replay and long-uptime video clock checks passed.");
    return 0;
}
