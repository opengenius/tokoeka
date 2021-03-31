#pragma once

#include "stdint.h"

struct hash_values_protocol_t {
    /**
     * Copy value from src_index to dst_index
     */
    void (*move)(void* ht_data, uint32_t dst_index, uint32_t src_index);
    /**
     * Clear value at index
     */
    void (*reset)(void* ht_data, uint32_t index);
};

struct hash_desc_t {
    uint32_t* hashes;
    void*     data;
    uint32_t  element_count;
};

struct hash32_find_iter_t {
    uint32_t index;
    uint32_t hash;
    uint32_t counter;
};
