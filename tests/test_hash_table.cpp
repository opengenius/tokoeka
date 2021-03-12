#include "catch2/catch.hpp"
#include "../src/hash_table.h"

struct pos_t {
    uint32_t row, column;
};

static bool operator==(const pos_t& p1, const pos_t& p2) {
    return p1.row == p2.row && p1.column == p2.column;
}

static bool operator!=(const pos_t& p1, const pos_t& p2) {
    return !(p1 == p2);
}

struct element_data_t {
    pos_t  pos;
    double value;
};

static uint32_t hash_uint32_t(const pos_t& pos) {
    return pos.column % 5;
}

static void element_data_move(void* ht_data, uint32_t dst_index, uint32_t src_index) {
    auto element_array = (element_data_t*)ht_data;

    element_array[dst_index] = element_array[src_index];
}

static void element_data_reset(void* ht_data, uint32_t index) {
    auto element_array = (element_data_t*)ht_data;

    auto& pos_at_index = element_array[index].pos;
    pos_at_index.row = 0u;
    pos_at_index.column = 0u;
}

const static hash_array_protocol_t s_element_data_impl = {
    element_data_move,
    element_data_reset
};

static uint32_t find_pos(const hash_desc_t& ht_desc, const pos_t& pos) {
    //assert(false && "chech for pos");
    return hash_find_index(&ht_desc, hash_uint32_t(pos));
}

TEST_CASE("insert-erase", "[hash_table]") {
    uint32_t hashes[20] = {};
    element_data_t elems[20] = {};
    
    hash_desc_t ht_desc = {};
    ht_desc.ht_api = &s_element_data_impl;
    ht_desc.hashes = hashes;
    ht_desc.data = elems;
    ht_desc.element_count = 20;

    // insert 1.0 at (2, 3)
    {
        pos_t p = {2, 3};
        auto index = find_pos(ht_desc, p);
        REQUIRE(index);
        REQUIRE(elems[index].value == 0.0);

        elems[index].pos = p;
        elems[index].value = 1.0;
    }

    // insert 2.0 at (3, 2)
    {
        pos_t p = {3, 2};
        auto index = find_pos(ht_desc, p);
        REQUIRE(index);
        REQUIRE(elems[index].value == 0.0);

        elems[index].pos = p;
        elems[index].value = 2.0;
    }

    // expect collision
    // insert 3.0 at (2, 2)
    {
        pos_t p = {2, 2};
        auto index = find_pos(ht_desc, p);
        REQUIRE(index);
        REQUIRE(elems[index].value == 0.0);

        elems[index].pos = p;
        elems[index].value = 3.0;
    }

    // look for (2, 2)
    {
        pos_t p = {2, 2};
        auto index = find_pos(ht_desc, p);
        REQUIRE(index);
        REQUIRE(elems[index].value == 3.0);
    }

    // erase for (2, 3)
    {
        pos_t p = {2, 3};
        auto index = find_pos(ht_desc, p);
        REQUIRE(index);
        REQUIRE(elems[index].value == 1.0);

        hash_erase(&ht_desc, index);
        // expect key to be removed
        REQUIRE(elems[index].pos != p);
    }
}