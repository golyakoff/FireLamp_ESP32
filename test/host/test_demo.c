// Host test of demo_core.c: next effect selection
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "demo_core.h"

#define N 6

int main(void)
{
    bool all[N] = { 1, 1, 1, 1, 1, 1 };
    bool some[N] = { 1, 0, 1, 0, 0, 1 };
    bool one[N] = { 0, 0, 0, 1, 0, 0 };

    // sequential, forward and backward, with wrap around
    assert(demo_pick_next(0, all, N, DEMO_ORDER_SEQUENTIAL, 1, 0) == 1);
    assert(demo_pick_next(5, all, N, DEMO_ORDER_SEQUENTIAL, 1, 0) == 0);
    assert(demo_pick_next(0, all, N, DEMO_ORDER_SEQUENTIAL, -1, 0) == 5);

    // excluded effects are skipped
    assert(demo_pick_next(0, some, N, DEMO_ORDER_SEQUENTIAL, 1, 0) == 2);
    assert(demo_pick_next(2, some, N, DEMO_ORDER_SEQUENTIAL, 1, 0) == 5);
    assert(demo_pick_next(5, some, N, DEMO_ORDER_SEQUENTIAL, 1, 0) == 0);
    assert(demo_pick_next(0, some, N, DEMO_ORDER_SEQUENTIAL, -1, 0) == 5);
    // current effect excluded (selected manually before the demo): go on from it
    assert(demo_pick_next(3, some, N, DEMO_ORDER_SEQUENTIAL, 1, 0) == 5);
    assert(demo_pick_next(3, some, N, DEMO_ORDER_SEQUENTIAL, -1, 0) == 2);

    // only one effect included: stay on it / go to it
    assert(demo_pick_next(3, one, N, DEMO_ORDER_SEQUENTIAL, 1, 0) == 3);
    assert(demo_pick_next(3, one, N, DEMO_ORDER_RANDOM, 1, 12345) == 3);
    assert(demo_pick_next(1, one, N, DEMO_ORDER_RANDOM, 1, 12345) == 3);
    assert(demo_pick_next(1, one, N, DEMO_ORDER_SEQUENTIAL, 1, 0) == 3);

    // random: never the current one, only included ones, every included one reachable
    int hits[N] = { 0 };
    for (uint32_t rnd = 0; rnd < 3000; rnd++)
    {
        size_t e = demo_pick_next(2, some, N, DEMO_ORDER_RANDOM, 1, rnd * 2654435761u);
        assert(e != 2 && some[e]);
        hits[e]++;
    }
    assert(hits[0] > 0 && hits[5] > 0);
    printf("demo: random picks from effect 2: e0=%d e5=%d\n", hits[0], hits[5]);

    printf("demo: all tests passed\n");
    return 0;
}
