#include "index_ht.h"
#include "hash_table.h"
#include <cstring>
#include <cassert>

namespace index_ht {

static void element_data_move(void* ht_data, uint32_t dst_index, uint32_t src_index) {
    auto indices = (uint32_t*)ht_data;
    indices[dst_index] = indices[src_index];
}

static void element_data_reset(void* ht_data, uint32_t index) {
    // no need to reset
}

static const hash_values_protocol_t s_term_ht_impl = {
    element_data_move,
    element_data_reset
};

void init(index_ht_t& self, uint32_t* hashes, uint32_t* indices, uint32_t size) {
    self.hashes = hashes;
    self.indices = indices;
    self.size = size;
    self.count = 0u;

    auto buffer_byte_size = sizeof(uint32_t) * size;
    memset(self.hashes, 0, buffer_byte_size);
}

uint32_t erase(index_ht_t& self, uint32_t ht_index) {
    assert(self.count);

    uint32_t value = self.indices[ht_index];

    hash_desc_t ht_desc = {};
    ht_desc.hashes = self.hashes;
    ht_desc.data = self.indices;
    ht_desc.element_count = self.size;
    auto erase_count = hash_erase(&s_term_ht_impl, &ht_desc, ht_index);

    --self.count;

    return value;
}

void insert(index_ht_t& self, uint32_t ht_index, uint32_t key_hash, uint32_t value) {
    // hash_desc_t ht_desc = {};
    // ht_desc.hashes = self.hashes;
    // ht_desc.data = self.indices;
    // ht_desc.element_count = self.size;
    // uint32_t move_count = hash_rh_insert_move(&s_term_ht_impl, &ht_desc, ht_index);
    // g_move_count = g_move_count < move_count ? move_count : g_move_count;

    self.hashes[ht_index] = key_hash;
    self.indices[ht_index] = value;

    ++self.count;
}

void rehash(index_ht_t& dst_ht, const index_ht_t& src_ht) {
    hash_desc_t ht_desc = {};
    ht_desc.hashes = dst_ht.hashes;
    ht_desc.element_count = dst_ht.size;

    for (size_t i = 0u; i < src_ht.size; ++i) {
        uint32_t el_hash = src_ht.hashes[i];
        if (!el_hash) continue;

        // find insert pos
        auto iter = hash_find_index(&ht_desc, el_hash);
        for (; iter.hash == el_hash; iter = hash_find_next(&ht_desc, &iter));

        insert(dst_ht, iter.index, el_hash, src_ht.indices[i]);
    }
}

}
