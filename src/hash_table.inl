#pragma once

#include <cassert>
#include "hash_types.h"

static hash32_find_iter_t hash_find_index(const hash_desc_t* desc, uint32_t key_hash) {
    assert(desc->hashes);
    assert(key_hash);

    hash32_find_iter_t res = {};

    res.index = key_hash & (desc->element_count - 1);
    for (res.counter = 0; res.counter < desc->element_count; ++res.counter) {
        res.hash = desc->hashes[res.index];
        if (!res.hash || res.hash == key_hash) {
            return res;
        }

        res.index = (res.index + 1) & (desc->element_count - 1);
    }

    res.index = ~0u;
    return res;
}

static hash32_find_iter_t hash_find_next(const hash_desc_t* desc, const hash32_find_iter_t* prev_iter) {
    assert(desc->hashes);
    assert(prev_iter->hash);

    hash32_find_iter_t res = {};

    res.index = (prev_iter->index + 1) & (desc->element_count - 1);
    for (res.counter = prev_iter->counter + 1; res.counter < desc->element_count; ++res.counter) {
        res.hash = desc->hashes[res.index];
        if (!res.hash || res.hash == prev_iter->hash) {
            return res;
        }

        res.index = (res.index + 1) & (desc->element_count - 1);
    }

    res.index = ~0u;
    return res;
}

static uint32_t hash_erase(const hash_values_protocol_t* ht_api, 
                    hash_desc_t* desc, uint32_t index) {
    assert(desc->hashes);
    assert(desc->data);

    uint32_t counter = 0;
    for (uint32_t i = (index + 1) & (desc->element_count - 1); i != index; i = (i + 1) & (desc->element_count - 1)) {
        if (!desc->hashes[i]) break;

        auto i_index = desc->hashes[i] & (desc->element_count - 1);

        if ( (i > index && (i_index <= index || i_index > i)) ||
             (i < index && (i_index <= index && i_index > i))) { 
            // swap
            desc->hashes[index] = desc->hashes[i];
            ht_api->move(desc->data, index, i);
            index = i;
        }

        counter++;
    }

    // clear index
    desc->hashes[index] = 0u;
    ht_api->reset(desc->data, index);

    return counter;
}
