#pragma once

#include "stdint.h"

struct hash_array_protocol_t {
    uint32_t (*hash)(const void* key);
    uint32_t (*hash_index)(void* ht_data, uint32_t index);
    bool (*key_equal)(void* ht_data, uint32_t index, const void* key);

    /**
     * Check if if key is set
     */
    bool (*key_valid)(void* ht_data, uint32_t index);

    /**
     * Copy element from src_index to dst_index
     */
    void (*move)(void* ht_data, uint32_t dst_index, uint32_t src_index);
    /**
     * Clear key fields
     */
    void (*reset)(void* ht_data, uint32_t index);
};

struct hash_desc_t {
    const hash_array_protocol_t* ht_api;

    void*    data;
    uint32_t element_count;
};

uint32_t g_find_max = 0;

static uint32_t hash_find_index(const hash_desc_t* desc, const void* key) {
    int key_hash = desc->ht_api->hash(key);
    int index = key_hash % desc->element_count;
    for (uint32_t i = 0; i < desc->element_count; ++i) {
        if (!desc->ht_api->key_valid(desc->data, index) || 
            desc->ht_api->key_equal(desc->data, index, key)) {

            g_find_max = g_find_max < i ? i : g_find_max;

            return index;
        }

        index = (index + 1) % desc->element_count;
    }

    return ~0u;
}

uint32_t g_erase_max = 0;

static void hash_erase(hash_desc_t* desc, uint32_t index) {
    uint32_t counter = 0;
    for (uint32_t i = (index + 1) % desc->element_count; i != index; i = (i + 1) % desc->element_count) {
        if (!desc->ht_api->key_valid(desc->data, i)) break;

        auto i_index = desc->ht_api->hash_index(desc->data, i) % desc->element_count;

        if ( (i > index && (i_index <= index || i_index > i)) ||
             (i < index && (i_index <= index && i_index > i))) { 
            // swap
            desc->ht_api->move(desc->data, index, i);
            index = i;
        }

        counter++;
    }

    // clear index
    desc->ht_api->reset(desc->data, index);

    g_erase_max = g_erase_max < counter ? counter : g_erase_max;
}
