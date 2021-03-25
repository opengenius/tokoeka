#pragma once

#include "stdint.h"

struct hash32_find_iter_t {
    uint32_t index;
    uint32_t hash;
    uint32_t counter;
};
