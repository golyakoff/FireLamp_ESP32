#include "demo_core.h"

size_t demo_pick_next(size_t current, const bool *included, size_t count, demo_order_t order, int dir, uint32_t rnd)
{
    // effects to choose from: included ones except the current
    size_t candidates = 0;
    for (size_t i = 0; i < count; i++)
        if (included[i] && i != current)
            candidates++;
    if (!candidates)
        return current;

    if (order == DEMO_ORDER_RANDOM)
    {
        size_t k = rnd % candidates;
        for (size_t i = 0; i < count; i++)
            if (included[i] && i != current && k-- == 0)
                return i;
    }

    // sequential: the nearest included effect in the given direction, wrapping around
    size_t i = current;
    for (size_t step = 0; step < count; step++)
    {
        i = dir < 0 ? (i + count - 1) % count : (i + 1) % count;
        if (included[i] && i != current)
            return i;
    }
    return current;
}
