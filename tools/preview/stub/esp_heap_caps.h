#pragma once
// heap statistics for the logs: none on a PC
#define MALLOC_CAP_8BIT 0
static inline unsigned heap_caps_get_free_size(int caps) { return 0; }
static inline unsigned heap_caps_get_largest_free_block(int caps) { return 0; }
