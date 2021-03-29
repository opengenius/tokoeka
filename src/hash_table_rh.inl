#include "hash_table.h"

static uint32_t distance(const hash_desc_t* desc, uint32_t key_hash, uint32_t pos) {
    auto desired_pos = key_hash % desc->element_count;
    auto dist = (desired_pos <= pos) ? pos - desired_pos : pos + (desc->element_count - desired_pos);
    // auto dist = (pos + desc->element_count - desired_pos) % desc->element_count;
    return dist;
}

static hash32_find_iter_t hash_rh_find_index(const hash_desc_t* desc, uint32_t key_hash) {
    assert(desc->hashes);
    assert(key_hash);

    hash32_find_iter_t res = {};

    res.index = key_hash % desc->element_count;
    for (res.counter = 0; res.counter < desc->element_count; ++res.counter) {
        res.hash = desc->hashes[res.index];
        if (!res.hash || res.hash == key_hash) {
            return res;
        }

        if (distance(desc, res.hash, res.index) < res.counter) return res;

        res.index = (res.index + 1) % desc->element_count;
    }

    res.index = ~0u;
    return res;
}

static hash32_find_iter_t hash_rh_find_next(const hash_desc_t* desc, const hash32_find_iter_t* prev_iter) {
    assert(desc->hashes);
    assert(prev_iter->hash);

    hash32_find_iter_t res = {};

    res.index = (prev_iter->index + 1) % desc->element_count;
    for (res.counter = prev_iter->counter + 1; res.counter < desc->element_count; ++res.counter) {
        res.hash = desc->hashes[res.index];
        if (!res.hash || res.hash == prev_iter->hash) {
            return res;
        }

        if (distance(desc, res.hash, res.index) < res.counter) return res;

        res.index = (res.index + 1) % desc->element_count;
    }

    res.index = ~0u;
    return res;
}

static bool hash_rh_insert_move(const hash_values_protocol_t* ht_api, 
                    hash_desc_t* desc, uint32_t index) {
    // find empty slot
    uint32_t empty_index = index;
    while (desc->hashes[empty_index])
    {
        empty_index = (empty_index + 1) % desc->element_count;
    }

    // slot is empty
    if (empty_index == index) return true;

    // move
    if (empty_index < index) {
        while (empty_index > 0) {
            auto prev_index = empty_index - 1;
            desc->hashes[empty_index] = desc->hashes[prev_index];
            ht_api->move(desc->data, empty_index, prev_index);
            empty_index = prev_index;
        }
        if (index < desc->element_count - 1) {
            empty_index = desc->element_count - 1;
            desc->hashes[0] = desc->hashes[empty_index];
            ht_api->move(desc->data, 0, empty_index);
        }
    }

    while(empty_index > index) {
        auto prev_index = empty_index - 1;
        desc->hashes[empty_index] = desc->hashes[prev_index];
        ht_api->move(desc->data, empty_index, prev_index);
        empty_index = prev_index;
    }

    return true;

    // // find new place for the current slot
    // uint32_t current_index = index;
    // uint32_t slot_dist = distance(desc, hash_at_index, index);
    // for (uint32_t counter = 0; counter < desc->element_count; ++counter) {
    //     ++slot_dist;

    //     uint32_t next_index = (current_index + slot_dist) % desc->element_count;
    //     auto next_hash = desc->hashes[next_index];

    //     // empty, fill with current and exit
    //     if (!next_hash) {
    //         desc->hashes[next_index] = desc->hashes[current_index];
    //         ht_api->move(desc->data, next_index, current_index);
    //         return true;
    //     }

    //     auto next_dist = distance(desc, next_hash, next_index);
    //     if (next_dist < slot_dist) {
    //         // rich found, swap
    //         desc->hashes[next_index] = desc->hashes[current_index];
    //         ht_api->move(desc->data, next_index, current_index);

    //         current_index = next_index;
    //         slot_dist = next_dist;
    //     }
    // }

    // not found empty slots, assert?
    return false;
}

static uint32_t hash_rh_erase(const hash_values_protocol_t* ht_api, 
                    hash_desc_t* desc, uint32_t index) {
    assert(desc->hashes);
    assert(desc->data);

    uint32_t counter = 0;
    for (uint32_t i = (index + 1) % desc->element_count; i != index; i = (i + 1) % desc->element_count) {
        const auto i_hash = desc->hashes[i];
        if (!i_hash) break;
        if (distance(desc, i_hash, i) == 0) break;

        //shift backward
        desc->hashes[index] = desc->hashes[i];
        ht_api->move(desc->data, index, i);
        index = i;

        counter++;
    }

    // clear index
    desc->hashes[index] = 0u;
    ht_api->reset(desc->data, index);

    return counter;
}
