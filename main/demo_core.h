#ifndef EVLAMP_DEMO_CORE_H_
#define EVLAMP_DEMO_CORE_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * Demo mode effect selection, pure logic
 */

typedef enum {
    DEMO_ORDER_SEQUENTIAL = 0,
    DEMO_ORDER_RANDOM,

    DEMO_ORDER_MAX
} demo_order_t;

/**
 * Pick the effect to play after `current`
 *
 * @param current Current effect number
 * @param included Which effects take part in the demo, `count` items
 * @param count Number of effects
 * @param order Sequential (`dir` = 1 forward, -1 backward) or random
 * @param dir Direction for the sequential order
 * @param rnd Random number for the random order
 * @return Next effect number; `current` if no other effect is included
 */
size_t demo_pick_next(size_t current, const bool *included, size_t count, demo_order_t order, int dir, uint32_t rnd);

#endif /* EVLAMP_DEMO_CORE_H_ */
