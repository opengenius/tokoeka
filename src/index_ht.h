#pragma once

#include <cstdint>

namespace index_ht {

struct index_ht_t {
    uint32_t* hashes;
    uint32_t* indices;
    uint32_t  size;
    uint32_t  count;
};

void init(index_ht_t& self, uint32_t* hashes, uint32_t* indices, uint32_t size);
uint32_t erase(index_ht_t& self, uint32_t ht_index);
void insert(index_ht_t& self, uint32_t ht_index, uint32_t key_hash, uint32_t value);
void rehash(index_ht_t& dst_ht, const index_ht_t& src_ht);

}
