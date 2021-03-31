#include "tokoeka/solver.h"

#include <cassert>
#include <cfloat>
#include <cstdlib>
#include <cstring>
#include "hash_table.inl"
#include "index_ht.h"

namespace tokoeka {

namespace {

static_assert(sizeof(num_t) == sizeof(double), "");
const num_t NUM_MAX = DBL_MAX;
const num_t NUM_EPS = 1e-6;
const uint32_t FREELIST_INDEX = 0u;

enum class symbol_type_e : uint8_t {
    EXTERNAL,
    SLACK,
    ERROR,
    DUMMY
};

struct var_data_t {
    symbol_type_e       type;
    constraint_handle_t constraint;
    num_t               edit_value;
};

struct constraint_data_t {
    symbol_t  marker;
    symbol_t  other; // nullable
    num_t     strength;
};

struct term_coord_t {
    symbol_t row, column;
};

static bool operator==(const term_coord_t& p1, const term_coord_t& p2) {
    return p1.row == p2.row && p1.column == p2.column;
}

struct term_data_t {
    term_coord_t pos;
    symbol_t     prev_row, next_row;
    symbol_t     prev_column, next_column;
    // padding: 4 bytes
    num_t        multiplier;
};

template<typename T>
struct array_t {
    T*     entries;
    size_t size; // max entries (size * sizeof(T) bytes)
};

/**
 * array_t with free list, 0 element is reserved for head
 */
template<typename T>
struct sparse_array_t {
    union entry_t
    {
        T        value;
        uint32_t next;
    };

    array_t<entry_t> array;
    uint32_t first_unused_index;
};

struct terms_table_t {
    sparse_array_t<term_data_t> terms;
    index_ht::index_ht_t indices;
};

} // internal namespace

struct solver_t {
    allocator_t allocator;

    sparse_array_t<var_data_t> vars;
    sparse_array_t<constraint_data_t> constraints;

    terms_table_t terms;
    symbol_t objective;
    symbol_t infeasible_rows; // use next constant term row links for infeasible rows
};

namespace {

/* utils */

static bool approx(num_t a, num_t b) { 
    return a > b ? a - b < NUM_EPS : b - a < NUM_EPS; 
}

static bool near_zero(num_t a) { 
    return approx(a, 0.0f);
}

/* allocator helper */

static void* allocate(allocator_t* allocator, size_t size) {
    return allocator->allocate(allocator->ud, size);
}

static void free(allocator_t* allocator, void* p) {
    allocator->free(allocator->ud, p);
}

/* array_t */

template<typename T>
static void free_array(allocator_t* alloc, array_t<T>* arr) {
    free(alloc, arr->entries);
}

template<typename T>
static size_t array_size(const array_t<T>* arr) {
    return arr->size;
}

template<typename T>
static T& array_get(array_t<T>& arr, size_t position) {
    assert(arr.entries);
    assert(position < arr.size);
    return arr.entries[position];
}

template<typename T>
static void array_grow(allocator_t* alloc, array_t<T>* arr, size_t new_size) {
    const size_t new_size_in_bytes = new_size * sizeof(T);
    T* new_buf = (T*)allocate(alloc, new_size_in_bytes);
    if (arr->entries) {
        memcpy(new_buf, arr->entries, new_size_in_bytes);
        free(alloc, arr->entries);
    }
    arr->entries = (T*)new_buf;
    arr->size = new_size;
}

/* sparse_array_t */

template<typename T>
static void array_init(allocator_t* alloc, sparse_array_t<T>& arr, size_t page_size) {
    array_grow(alloc, &arr.array, page_size / sizeof(typename sparse_array_t<T>::entry_t));
    auto& free_list_head_entry = array_get(arr.array, FREELIST_INDEX);
    free_list_head_entry.next = 0u;
    arr.first_unused_index = 1u;
}

template<typename T>
static void free_array(allocator_t* alloc, sparse_array_t<T>& arr) {
    free_array(alloc, &arr.array);
    arr.first_unused_index = 0u;
}

template<typename T>
static T& array_get(sparse_array_t<T>& arr, size_t position) {
    return array_get(arr.array, position).value;
}

template<typename T>
static uint32_t array_add_no_grow(sparse_array_t<T>& arr, const T& v) {
    uint32_t new_index = {};
    // try free list
    auto& free_list_head_entry = array_get(arr.array, FREELIST_INDEX); 
    if (free_list_head_entry.next) {
        new_index = free_list_head_entry.next;
        auto& el = array_get(arr.array, new_index); 
        free_list_head_entry.next = el.next;

    // try unused elements
    } else if (arr.first_unused_index < array_size(&arr.array)) {
        new_index = arr.first_unused_index++;

    // grow
    } else {
        return new_index;
    }

    array_get(arr, new_index) = v;

    return new_index;
}

template<typename T>
static uint32_t array_add(allocator_t* alloc, sparse_array_t<T>& arr, const T& v) {
    uint32_t new_index = array_add_no_grow(arr, v);
    if (!new_index) {
        auto new_size = array_size(&arr.array) * 2;
        array_grow(alloc, &arr.array, new_size);

        new_index = arr.first_unused_index++;
        array_get(arr, new_index) = v;
    }

    return new_index;
}

template<typename T>
static void array_remove(sparse_array_t<T>& arr, uint32_t index) {
    assert(index);
    auto& free_list_head_entry = array_get(arr.array, FREELIST_INDEX);
    auto& at_index_entry = array_get(arr.array, index); 
    at_index_entry.next = free_list_head_entry.next;
    free_list_head_entry.next = index;
}

///////////////////////////////////////////////////////////////////////////////
// Term hash table 
///////////////////////////////////////////////////////////////////////////////

/// hash a single byte
static uint32_t fnv1a_hash(uint8_t oneByte, uint32_t hash) {
    const uint32_t Prime = 0x01000193; //   16777619
    return (oneByte ^ hash) * Prime;
}

const uint32_t fnv1a_seed  = 0x811C9DC5; // 2166136261
static uint32_t fnv1a_hash(uint16_t twoBytes, uint32_t hash = fnv1a_seed) {
    const uint8_t* ptr = (const uint8_t*) &twoBytes;
    hash = fnv1a_hash(*ptr++, hash);
    return fnv1a_hash(*ptr  , hash);
}

static uint32_t hash_uint32_t(const term_coord_t& pos) {
    uint32_t hash = fnv1a_hash(pos.row);
    uint32_t res = fnv1a_hash(pos.column, hash);

    res += (res == 0u) ? 1u : 0u;
    return res;
}

///////////////////////////////////////////////////////////////////////////////
// Linear equation tableau (sparse matrix in DOK with row, column linked lists)
///////////////////////////////////////////////////////////////////////////////

static void init_table(allocator_t* alloc, terms_table_t* terms, size_t page_size) {
    array_init(alloc, terms->terms, page_size);

    uint32_t size = (uint32_t)page_size / (sizeof(uint32_t) * 2);
    uint32_t* indices_buf = (uint32_t*)allocate(alloc, sizeof(uint32_t) * size * 2);
    index_ht::init(terms->indices, indices_buf, indices_buf + size, size);
}

static void free_table(allocator_t* alloc, terms_table_t* terms) {
    free(alloc, terms->indices.hashes); // hashes + indices chunk
    free_array(alloc, terms->terms);
}

typedef struct {
    uint32_t index;
    bool found;
} index_result_t;

static index_result_t get_term_index_no_assert(terms_table_t* terms, const term_coord_t& coord) {
    hash_desc_t ht_desc = {};
    ht_desc.hashes = terms->indices.hashes;
    ht_desc.element_count = terms->indices.size;

    auto coord_h = hash_uint32_t(coord);
    auto iter = hash_find_index(&ht_desc, coord_h);
    for (; iter.hash == coord_h; 
            iter = hash_find_next(&ht_desc, &iter)) {
        auto term_index = terms->indices.indices[iter.index];
        if (coord == array_get(terms->terms, term_index).pos) {
            return {iter.index, true};
        }
    }

    return {iter.index, false};
}

static term_data_t* get_term(terms_table_t* terms, const term_coord_t& coord, uint32_t* out_index = nullptr) {
    auto index_res = get_term_index_no_assert(terms, coord);
    assert(index_res.found);
    auto term_pos = terms->indices.indices[index_res.index];
    term_data_t* res = &array_get(terms->terms, term_pos);
    assert(res->pos.row == coord.row && res->pos.column == coord.column);

    if (out_index) *out_index = index_res.index;
    return res;
}

static void table_grow_rehash(allocator_t* alloc, index_ht::index_ht_t* indices) {
    auto new_size = indices->size * 2;
    uint32_t* indices_buf = (uint32_t*)allocate(alloc, sizeof(uint32_t) * new_size * 2);
    index_ht::index_ht_t new_indices = {};
    index_ht::init(new_indices, indices_buf, indices_buf + new_size, new_size);

    index_ht::rehash(new_indices, *indices);
    free(alloc, indices->hashes); // hashes + indices chunk

    *indices = new_indices;
}

typedef struct {
    term_data_t* term;
    uint32_t     index;
} term_result_t;

static term_result_t get_term_result(terms_table_t* terms, const term_coord_t& coord) {
    uint32_t index = 0u;
    auto term = get_term(terms, coord, &index);
    return {term, index};
}

static term_result_t find_term(terms_table_t* terms, const term_coord_t& coord) {
    term_result_t res = {};
    auto [index, found] = get_term_index_no_assert(terms, coord);
    res.index = index;
    if (found) {
        auto index = terms->indices.indices[res.index];
        res.term = &array_get(terms->terms, index);
    }

    return res;
}

static term_data_t* find_existing_term(terms_table_t* terms, const term_coord_t& coord) {
     term_result_t res = find_term(terms, coord);
     if (res.term && 
        res.term->pos.row == coord.row &&
        res.term->pos.column == coord.column) {
            return res.term;
        }

    return nullptr;
}

static int is_constant_row(terms_table_t* terms, symbol_t row) {
    term_coord_t key = {row, 0u};
    auto term = get_term(terms, key);
    return term->next_column == 0u;
}

typedef struct {
    term_result_t term_res;
    term_coord_t next_coord;
} term_iterator_t;

static term_iterator_t first_symbol_iterator(terms_table_t* terms, symbol_t sym) {
    term_iterator_t res = {};

    auto sym_list = get_term(terms, {0u, sym});
    if (!sym_list->next_row) return res;

    res.term_res = get_term_result(terms, {sym_list->next_row, sym});
    res.next_coord = {res.term_res.term->next_row, sym};

    return res;
}

static term_iterator_t next_symbol_iterator(terms_table_t* terms, const term_iterator_t& iter) {
    term_iterator_t res = {};

    if (!iter.next_coord.row) return res;

    res.term_res = get_term_result(terms, iter.next_coord);
    res.next_coord = {res.term_res.term->next_row, iter.next_coord.column};

    return res;
}

typedef struct {
    term_result_t term_res;
    term_coord_t next_coord;
} term_row_iterator_t;

static term_row_iterator_t first_row_iterator(terms_table_t* terms, symbol_t row) {
    term_row_iterator_t res = {};

    res.term_res = get_term_result(terms, {row, 0u});
    res.next_coord = {row, res.term_res.term->next_column};

    return res;
}

static term_row_iterator_t next_row_iterator(terms_table_t* terms, const term_row_iterator_t& iter) {
    term_row_iterator_t res = {};

    if (!iter.next_coord.column) return res;

    res.term_res = get_term_result(terms, iter.next_coord);
    res.next_coord = {iter.next_coord.row, res.term_res.term->next_column};

    return res;
}

static term_row_iterator_t first_row_term_iterator(terms_table_t* terms, symbol_t row) {
    term_row_iterator_t res = first_row_iterator(terms, row);
    return next_row_iterator(terms, res);
}

static void link_term(terms_table_t* terms, term_coord_t coord, term_data_t& new_term) {
    // link in row
    {
        // update head link
        term_coord_t row_head_key = {coord.row, 0};
        auto row_head_term = get_term(terms, row_head_key);

        auto last_col = row_head_term->prev_column;
        row_head_term->prev_column = coord.column;

        // update tail link
        term_coord_t row_tail_key = {coord.row, last_col};
        auto tail_term = last_col == 0u ? row_head_term : get_term(terms, row_tail_key);

        assert(tail_term->next_column == 0u);
        tail_term->next_column = coord.column;

        // new term links
        new_term.prev_column = last_col;
        new_term.next_column = 0u;
    }

    // link in var list
    {
        // update head link
        term_coord_t col_key = {0, coord.column};
        auto col_term = get_term(terms, col_key);

        auto last_row = col_term->prev_row;
        col_term->prev_row = coord.row;

        // update tail link
        term_coord_t tail_key = {last_row, coord.column};
        auto tail_term = last_row == 0u ? col_term : get_term(terms, tail_key);

        assert(tail_term->next_row == 0u);
        tail_term->next_row = coord.row;

        // new term links
        new_term.prev_row = last_row;
        new_term.next_row = 0u;
    }
}

enum unlink_frags_e : uint8_t {
    NONE = 0,
    ROW = 1 << 0,
    COLUMN = 1 << 1,
    BOTH = ROW|COLUMN
};

static void unlink_term(terms_table_t* terms, const term_data_t* t, unlink_frags_e unlink_flag) {
    // unlink row
    if (unlink_flag & unlink_frags_e::ROW) {
        assert(get_term(terms, {t->pos.row, t->prev_column})->next_column == t->pos.column);
        assert(get_term(terms, {t->pos.row, t->next_column})->prev_column == t->pos.column);

        term_coord_t prev_coord = {t->pos.row, t->prev_column};
        auto prev_term = get_term(terms, prev_coord);

        prev_term->next_column = t->next_column;

        term_coord_t next_coord = {t->pos.row, t->next_column};
        auto next_term = t->prev_column == t->next_column ? prev_term : get_term(terms, next_coord);
        next_term->prev_column = t->prev_column;

        assert(get_term(terms, {t->pos.row, t->prev_column})->next_column == t->next_column);
        assert(get_term(terms, {t->pos.row, t->next_column})->prev_column == t->prev_column);
    }

    // unlink column
    if (unlink_flag & unlink_frags_e::COLUMN) {
        term_coord_t prev_coord = {t->prev_row, t->pos.column};
        auto prev_term = get_term(terms, prev_coord);
        prev_term->next_row = t->next_row;

        term_coord_t next_coord = {t->next_row, t->pos.column};
        auto next_term = t->prev_row == t->next_row ? prev_term : get_term(terms, next_coord);
        next_term->prev_row = t->prev_row;
    }
}

static void delete_term(terms_table_t* terms, const term_result_t* term_it, 
                        unlink_frags_e unlink_flag = unlink_frags_e::BOTH) {
    unlink_term(terms, term_it->term, unlink_flag);
    auto term_pos = index_ht::erase(terms->indices, term_it->index);
    array_remove(terms->terms, term_pos);
}

static void free_row(terms_table_t* terms, symbol_t row) {
    for (auto term_it = first_row_iterator(terms, row); 
            term_it.term_res.term;
            term_it = next_row_iterator(terms, term_it)) {
        bool first = term_it.term_res.term->pos.column == 0;
        delete_term(terms, &term_it.term_res, first ? unlink_frags_e::NONE : unlink_frags_e::COLUMN);
    }
}

static void multiply_row(terms_table_t* terms, symbol_t row, num_t multiplier) {
    for (auto term_it = first_row_iterator(terms, row); 
            term_it.term_res.term;
            term_it = next_row_iterator(terms, term_it)) {
        auto term_ptr = term_it.term_res.term;

        term_ptr->multiplier *= multiplier;
    }
}

static void add_term(allocator_t* alloc, terms_table_t* terms, symbol_t row, symbol_t sym, num_t value) {
    const term_coord_t key = {row, sym};
    auto var_term_it = find_term(terms, key);
    assert(var_term_it.index != ~0u && "expect ht to grow?");

    if (!var_term_it.term) {
        // no var, add

        term_data_t new_term = {};
        new_term.pos = key;
        if (row && sym) {
            // todo: cache row terms for multiple add_term calls for single row
            link_term(terms, key, new_term);
        }


        auto new_term_index = array_add(alloc, terms->terms, new_term);
        assert(new_term_index);
        
        if (terms->indices.size < terms->indices.count * 2) {
            table_grow_rehash(alloc, &terms->indices);
            var_term_it = find_term(terms, key);
        }
        index_ht::insert(terms->indices, var_term_it.index, hash_uint32_t(key), new_term_index);
        var_term_it.term = &array_get(terms->terms, new_term_index);
    }

    var_term_it.term->multiplier += value;
    if (row && sym && near_zero(var_term_it.term->multiplier)) {
        // delete key
        delete_term(terms, &var_term_it);
    }
}

static void add_row(allocator_t* alloc, terms_table_t* terms, symbol_t row, symbol_t other, num_t multiplier) {
    for (auto term_it = first_row_iterator(terms, other); 
            term_it.term_res.term;
            term_it = next_row_iterator(terms, term_it)) {
        auto term_ptr = term_it.term_res.term;

        add_term(alloc, terms, row, term_ptr->pos.column, term_ptr->multiplier * multiplier);
    }
}

static bool has_row(terms_table_t* terms, symbol_t row) {
    return find_existing_term(terms, {row, 0u}) != nullptr;
}

static void merge_row(allocator_t* alloc, terms_table_t* terms, symbol_t row, symbol_t var, num_t multiplier) {
    if (has_row(terms, var))
        add_row(alloc, terms, row, var, multiplier);
    else
        add_term(alloc, terms, row, var, multiplier);
}

static void init_row(allocator_t* alloc, terms_table_t* terms, symbol_t row, num_t constant) {
    assert(!has_row(terms, row));

    // row is defined with constant term
    add_term(alloc, terms, row, 0u, constant);
}

///////////////////////////////////////////////////////////////////////////////
// Solver implementation
///////////////////////////////////////////////////////////////////////////////

static var_data_t* get_var_data(solver_t *solver, symbol_t var) {
    assert(solver);
    assert(var);

    return &array_get(solver->vars, var);
}

static bool is_external(solver_t* solver, symbol_t key) {
    return get_var_data(solver, key)->type == symbol_type_e::EXTERNAL;
}

static bool is_slack(solver_t* solver, symbol_t key) {
    return get_var_data(solver, key)->type == symbol_type_e::SLACK;
}

static bool is_error(solver_t* solver, symbol_t key) {
    return get_var_data(solver, key)->type == symbol_type_e::ERROR;
}

static bool is_dummy(solver_t* solver, symbol_t key) {
    return get_var_data(solver, key)->type == symbol_type_e::DUMMY;
}

static bool is_pivotable(solver_t* solver, symbol_t key) {
    return (is_slack(solver, key) || is_error(solver, key));
}

static symbol_t new_symbol(solver_t *solver, symbol_type_e type) {
    var_data_t data = {};
    data.type = type;
    symbol_t id = array_add(&solver->allocator, solver->vars, data);

    // init symbol link list
    add_term(&solver->allocator, &solver->terms, 0u, id, 0.0f);

    return id;
}

static constraint_data_t* constraint_data(solver_t *solver, constraint_handle_t cons_id) {
    return &array_get(solver->constraints, cons_id);
}

/* Cassowary algorithm */

static void mark_infeasible(solver_t *solver, term_data_t* row_term) {
    if (row_term->multiplier < 0.0f && !row_term->next_row) {
        row_term->next_row = solver->infeasible_rows ? solver->infeasible_rows : row_term->pos.row;
        solver->infeasible_rows = row_term->pos.row;
    }
}

static void mark_infeasible(solver_t *solver, symbol_t row) {
    auto row_term = get_term(&solver->terms, {row, 0u});
    mark_infeasible(solver, row_term);
}

static void pivot(solver_t *solver, symbol_t row, symbol_t entry, symbol_t exit) {
    assert(!has_row(&solver->terms, entry));

    term_coord_t key = {row, entry};
    auto term_it = get_term_result(&solver->terms, key);
    num_t reciprocal = 1.0f / term_it.term->multiplier;
    assert(entry != exit && !near_zero(term_it.term->multiplier));
    delete_term(&solver->terms, &term_it);

    add_row(&solver->allocator, &solver->terms, entry, row, -reciprocal);
    free_row(&solver->terms, row);
    if (row != exit) delete_variable(solver, row);

    if (exit != 0) add_term(&solver->allocator, &solver->terms, entry, exit, reciprocal);

    for (auto sym_iter = first_symbol_iterator(&solver->terms, entry); 
            sym_iter.term_res.term; 
            sym_iter = next_symbol_iterator(&solver->terms, sym_iter) ) {

        symbol_t it_row = sym_iter.term_res.term->pos.row;
        auto term_multiplier = sym_iter.term_res.term->multiplier;

        // substitute entry term with solved row
        delete_term(&solver->terms, &sym_iter.term_res, unlink_frags_e::ROW);
        add_row(&solver->allocator, &solver->terms, it_row, entry, term_multiplier);
        
        // mark row as infeasible, skip objective as well
        if (!is_external(solver, it_row))
            mark_infeasible(solver, it_row);
    }

    // reset entry symbol list as symbol links were not updated in delete_term
    auto entry_list_term = get_term(&solver->terms, {0, entry});
    entry_list_term->next_row = entry_list_term->prev_row = 0u;
}

static result_e optimize(solver_t *solver, symbol_t objective) {
    for (;;) {
        symbol_t enter = 0u, exit = 0u;
        num_t r, min_ratio = NUM_MAX;

        assert(solver->infeasible_rows == 0);

        for (auto term_it = first_row_term_iterator(&solver->terms, objective);
                term_it.term_res.term;
                term_it = next_row_iterator(&solver->terms, term_it)) {
            auto term_ptr = term_it.term_res.term;

            if (!is_dummy(solver, term_ptr->pos.column) && 
                    term_ptr->multiplier < 0.0f) { 
                enter = term_ptr->pos.column; 
                break; 
            }        
        }

        if (enter == 0) return result_e::OK;

        // enter symbol const iteration
        for (auto sym_iter = first_symbol_iterator(&solver->terms, enter); 
                sym_iter.term_res.term; 
                sym_iter = next_symbol_iterator(&solver->terms, sym_iter) ) {

            symbol_t it_row = sym_iter.term_res.term->pos.row;
            auto term_multiplier = sym_iter.term_res.term->multiplier;

            if (!is_pivotable(solver, it_row) || 
                    it_row == objective ||
                    term_multiplier > 0.0f) 
                continue;

            r = -value(solver, it_row) / term_multiplier;
            if (r < min_ratio || (approx(r, min_ratio)
                        && it_row < exit)) {
                min_ratio = r;
                exit = it_row;
            }
        }

        assert(exit != 0);
        if (exit == 0) return result_e::FAILED;

        pivot(solver, exit, enter, exit);
    }
}

static symbol_t make_row(solver_t *solver, const constraint_desc_t* desc, constraint_data_t* cons) {
    // use temp var to form the row
    symbol_t row = new_symbol(solver, symbol_type_e::SLACK);
    init_row(&solver->allocator, &solver->terms, row, -desc->constant);
    for (size_t i = 0; i < desc->term_count; ++i) {
        merge_row(&solver->allocator, &solver->terms, row, desc->symbols[i], desc->multipliers[i]);
    }

    if (desc->relation != relation_e::EQUAL) {
        num_t coeff = desc->relation == relation_e::LESSEQUAL ? 1.0f : -1.0f;
        cons->marker = new_symbol(solver, symbol_type_e::SLACK);
        add_term(&solver->allocator, &solver->terms, row, cons->marker, coeff);
        if (cons->strength < STRENGTH_REQUIRED) {
            cons->other = new_symbol(solver, symbol_type_e::ERROR);
            add_term(&solver->allocator, &solver->terms, row, cons->other, -coeff);
            add_term(&solver->allocator, &solver->terms, solver->objective, cons->other, cons->strength);
        }
    } else if (cons->strength >= STRENGTH_REQUIRED) {
        cons->marker = new_symbol(solver, symbol_type_e::DUMMY);
        add_term(&solver->allocator, &solver->terms, row, cons->marker, 1.0f);
    } else {
        cons->marker = new_symbol(solver, symbol_type_e::ERROR);
        cons->other = new_symbol(solver, symbol_type_e::ERROR);
        add_term(&solver->allocator, &solver->terms, row, cons->marker, -1.0f);
        add_term(&solver->allocator, &solver->terms, row, cons->other,   1.0f);
        add_term(&solver->allocator, &solver->terms, solver->objective, cons->marker, cons->strength);
        add_term(&solver->allocator, &solver->terms, solver->objective, cons->other,  cons->strength);
    }
    if (value(solver, row) < 0.0f) multiply_row(&solver->terms, row, -1.0f);
    return row;
}

static void remove_errors(solver_t *solver, constraint_data_t *cons) {
    if (is_error(solver, cons->marker))
        merge_row(&solver->allocator, &solver->terms, solver->objective, cons->marker, -cons->strength);
    if (cons->other && is_error(solver, cons->other))
        merge_row(&solver->allocator, &solver->terms, solver->objective, cons->other, -cons->strength);
    if (is_constant_row(&solver->terms, solver->objective)) {
        auto obj_constant_term = get_term(&solver->terms, {solver->objective, 0u});
        obj_constant_term->multiplier = 0.0f;
    }
}

// const
static symbol_t get_leaving_row(solver_t *solver, symbol_t marker) {
    symbol_t first = 0u, second = 0u, third = 0u;
    num_t r1 = NUM_MAX, r2 = NUM_MAX;

    // marker symbol const iteration
    for (auto sym_iter = first_symbol_iterator(&solver->terms, marker); 
            sym_iter.term_res.term; 
            sym_iter = next_symbol_iterator(&solver->terms, sym_iter) ) {

        symbol_t it_row = sym_iter.term_res.term->pos.row;
        auto term_multiplier = sym_iter.term_res.term->multiplier;

        if (is_external(solver, it_row))
            third = it_row;
        else if (term_multiplier < 0.0f) {
            num_t r = -value(solver, it_row) / term_multiplier;
            if (r < r1) r1 = r, first = it_row;
        } else {
            num_t r = value(solver, it_row) / term_multiplier;
            if (r < r2) r2 = r, second = it_row;
        }
    }
    return first ? first : second ? second : third;
}

static void remove_vars(solver_t *solver, constraint_handle_t cons) {
    if (!cons) return;

    auto cons_data = constraint_data(solver, cons);
    assert(cons_data->marker);

    symbol_t marker = cons_data->marker;
    remove_errors(solver, cons_data);

    if (!has_row(&solver->terms, marker)) {
        symbol_t exit = get_leaving_row(solver, marker);
        assert(exit != 0);
        pivot(solver, exit, marker, exit);
    }
    free_row(&solver->terms, marker);
    delete_variable(solver, cons_data->marker);
    delete_variable(solver, cons_data->other);

    optimize(solver, solver->objective);
}

static result_e add_with_artificial(solver_t *solver, symbol_t row) {
    symbol_t a = new_symbol(solver, symbol_type_e::SLACK); /* artificial variable will be removed */
    add_row(&solver->allocator, &solver->terms, a, row, 1.0f);

    optimize(solver, row);
    result_e ret = near_zero(value(solver, row)) ? result_e::OK : result_e::UNBOUND;
    free_row(&solver->terms, row);
    delete_variable(solver, row);
    if (has_row(&solver->terms, a)) {
        if (is_constant_row(&solver->terms, a)) { 
            free_row(&solver->terms, a);
            delete_variable(solver, a);
            return ret; 
        }

        symbol_t entry = 0u;

        for (auto term_it = first_row_term_iterator(&solver->terms, a);
                term_it.term_res.term;
                term_it = next_row_iterator(&solver->terms, term_it)) {
            auto term_ptr = term_it.term_res.term;

            if (is_pivotable(solver, term_ptr->pos.column)) { 
                entry = term_ptr->pos.column; 
                break; 
            }
        }

        if (!entry) { 
            free_row(&solver->terms, a); 
            delete_variable(solver, a);
            return result_e::UNBOUND; 
        }
        pivot(solver, a, entry, 0u);
    }

    // remove artificial variable column
    for (auto sym_iter = first_symbol_iterator(&solver->terms, a); 
            sym_iter.term_res.term; 
            sym_iter = next_symbol_iterator(&solver->terms, sym_iter) ) {
        delete_term(&solver->terms, &sym_iter.term_res, unlink_frags_e::ROW);
    }
    // reset next row to pass delete_variable assert, ifdef with NDEBUG?
    get_term(&solver->terms, {0, a})->next_row = 0u;
    delete_variable(solver, a);
    
    return ret;
}

static symbol_t choose_subject(solver_t *solver, symbol_t row, const constraint_data_t *cons, bool* out_all_dummy) {
    bool all_terms_dummy = true;
    for (auto term_it = first_row_term_iterator(&solver->terms, row);
            term_it.term_res.term;
            term_it = next_row_iterator(&solver->terms, term_it)) {
        auto term_ptr = term_it.term_res.term;

        auto term_key = term_ptr->pos.column;
        if (is_external(solver, term_key)) { 
            return term_key;  
        }

        all_terms_dummy = all_terms_dummy && is_dummy(solver, term_key);
    }

    if (is_pivotable(solver, cons->marker)) {
        term_data_t *mterm = get_term(&solver->terms, {row, cons->marker});
        if (mterm->multiplier < 0.0f) return cons->marker;
    }
    if (cons->other && is_pivotable(solver, cons->other)) {
        term_data_t *mterm = get_term(&solver->terms, {row, cons->other});
        if (mterm->multiplier < 0.0f) return cons->other;
    }

    // this makes sense only if no subject was found
    *out_all_dummy = all_terms_dummy;
    return 0u;
}

static result_e try_addrow(solver_t *solver, symbol_t row, const constraint_data_t *cons) {
    bool all_terms_dummy = false;
    symbol_t subject = choose_subject(solver, row, cons, &all_terms_dummy);
    if (!subject && all_terms_dummy) {
        if (near_zero(value(solver, row)))
            subject = cons->marker;
        else {
            free_row(&solver->terms, row);
            delete_variable(solver, row);
            return result_e::UNSATISFIED;
        }
    }
    if (!subject)
        return add_with_artificial(solver, row);
    pivot(solver, row, subject, 0u);
    return result_e::OK;
}

static void delta_edit_constant(solver_t *solver, num_t delta, constraint_handle_t cons_id) {
    auto cons = constraint_data(solver, cons_id);

    auto row_term = find_existing_term(&solver->terms, {cons->marker, 0u});
    if (row_term) { 
        row_term->multiplier -= delta; 
        mark_infeasible(solver, row_term); 
        return; 
    }

    // cons->other always not null for edit var constraint
    row_term = find_existing_term(&solver->terms, {cons->other, 0u});
    if (row_term) { 
        row_term->multiplier += delta; 
        mark_infeasible(solver, row_term); 
        return; 
    }

    // marker symbol const iteration
    for (auto sym_iter = first_symbol_iterator(&solver->terms, cons->marker); 
            sym_iter.term_res.term; 
            sym_iter = next_symbol_iterator(&solver->terms, sym_iter) ) {

        symbol_t it_row = sym_iter.term_res.term->pos.row;
        auto term_multiplier = sym_iter.term_res.term->multiplier;

        auto row_const_term = get_term(&solver->terms, {it_row, 0u});

        row_const_term->multiplier += term_multiplier * delta;
        if (!is_external(solver, it_row)) {
            mark_infeasible(solver, row_const_term);
        }
    }
}

static void dual_optimize(solver_t *solver) {
    while (solver->infeasible_rows != 0) {
        symbol_t cur, enter = 0u, leave;
        num_t r, min_ratio = NUM_MAX;
        symbol_t row = solver->infeasible_rows;

        auto row_const_term = get_term(&solver->terms, {row, 0u});

        leave = row;
        solver->infeasible_rows = row_const_term->next_row != row ? row_const_term->next_row : 0u;
        row_const_term->next_row = 0u;

        if (near_zero(row_const_term->multiplier) || 
                row_const_term->multiplier >= 0.0f) 
            continue;

        for (auto term_it = first_row_term_iterator(&solver->terms, row);
                term_it.term_res.term;
                term_it = next_row_iterator(&solver->terms, term_it)) {
            auto term_ptr = term_it.term_res.term;

            cur = term_ptr->pos.column;
            if (is_dummy(solver, cur) || term_ptr->multiplier <= 0.0f)
                continue;
            auto objterm = find_existing_term(&solver->terms, {solver->objective, cur});
            r = objterm ? objterm->multiplier / term_ptr->multiplier : 0.0f;
            if (min_ratio > r) min_ratio = r, enter = cur;
        }
        assert(enter != 0);
        pivot(solver, leave, enter, leave);
    }
}

static void* default_allocate(void *ud, size_t size) {
    return malloc(size);
}

static void default_free(void *ud, void* p) {
    ::free(p);
}

static const allocator_t s_default_allocator = {
    default_allocate,
    default_free,
    nullptr
};

} // internal namespace

/**
 * Public interface implementation
 */

solver_t *create_solver(const solver_desc_t* desc) {
    assert(desc);

    allocator_t allocator = desc->allocator;
    if (!allocator.allocate) {
        allocator = s_default_allocator;
    }
    solver_t* solver = (solver_t*)allocate(&allocator, sizeof(solver_t));
    memset(solver, 0, sizeof(*solver));
    solver->allocator = allocator;

    // reserve single page size variable array
    const uint32_t PAGE_SIZE = desc->page_size ? desc->page_size : 4096;
    assert(!(PAGE_SIZE & (PAGE_SIZE - 1)) && "expect power of 2 size");
    array_init(&solver->allocator, solver->vars, PAGE_SIZE);
    array_init(&solver->allocator, solver->constraints, PAGE_SIZE);

    init_table(&solver->allocator, &solver->terms, PAGE_SIZE);
    
    // init objective function
    solver->objective = new_symbol(solver, symbol_type_e::EXTERNAL);
    init_row(&solver->allocator, &solver->terms, solver->objective, 0.0f);

    return solver;
}

void destroy_solver(solver_t *solver) {
    assert(solver);

    free_array(&solver->allocator, solver->vars);
    free_array(&solver->allocator, solver->constraints);
    free_table(&solver->allocator, &solver->terms);

    free(&solver->allocator, solver);
}

symbol_t create_variable(solver_t *solver) {
    assert(solver);
    return new_symbol(solver, symbol_type_e::EXTERNAL);
}

void delete_variable(solver_t *solver, symbol_t var) {
    assert(solver);
    if (!var) return;

    const auto& var_data = array_get(solver->vars, var); 
    remove_constraint(solver, var_data.constraint);

    // todo: delete rows? 
    assert(!has_row(&solver->terms, var));
    assert(!first_symbol_iterator(&solver->terms, var).term_res.term);

    // delete symbol link list
    auto term_it = get_term_result(&solver->terms, {0u, var});
    delete_term(&solver->terms, &term_it, unlink_frags_e::NONE);

    // link to free list
    array_remove(solver->vars, var);
}

num_t value(solver_t *solver, symbol_t var) {
    assert(solver);
    assert(var);

    const term_coord_t key = {var, 0u};
    auto var_term = find_existing_term(&solver->terms, key);
    if (var_term) {
        return var_term->multiplier;
    }

    return 0.0f;
}

result_e add_constraint(solver_t *solver, const constraint_desc_t* desc, constraint_handle_t *out_cons) {
    assert(solver);
    assert(desc);
    assert(out_cons);

    constraint_data_t cons_data = {};
    cons_data.strength = desc->strength;
    symbol_t row = make_row(solver, desc, &cons_data);
    result_e ret = try_addrow(solver, row, &cons_data);
    if (ret != result_e::OK) {
        // todo: test this path
        remove_errors(solver, &cons_data);
        delete_variable(solver, cons_data.marker);
        delete_variable(solver, cons_data.other);

        return ret;
    } else {
        optimize(solver, solver->objective);
    }

    *out_cons = array_add(&solver->allocator, solver->constraints, cons_data);

    assert(solver->infeasible_rows == 0);
    return ret;
}

void remove_constraint(solver_t *solver, constraint_handle_t cons) {
    assert(solver);
    if (!cons) return;

    remove_vars(solver, cons);

    // link to free list
    array_remove(solver->constraints, cons);
}

result_e enable_edit(solver_t *solver, symbol_t var, num_t strength) {
    strength = (strength >= STRENGTH_STRONG) ? STRENGTH_STRONG : strength;

    auto var_data = get_var_data(solver, var);
    if (var_data->constraint) {
        remove_constraint(solver, var_data->constraint);
    }

    symbol_t symbols[] = {var};
    num_t multipiers[] = {1.0f};

    constraint_desc_t desc = {};
    desc.strength = strength;
    desc.term_count = 1;
    desc.symbols = symbols;
    desc.multipliers = multipiers;
    desc.relation = relation_e::EQUAL;

    constraint_handle_t cons;
    auto res = add_constraint(solver, &desc, &cons);
    assert(res == result_e::OK && "must pivot to var or constraint marker/error symbol");

    var_data = get_var_data(solver, var);
    var_data->constraint = cons;
    var_data->edit_value = 0u;

    return result_e::OK;
}

void disable_edit(solver_t *solver, symbol_t var) {
    if (var == 0) return;
    
    auto var_data = get_var_data(solver, var);
    auto var_constraint = var_data->constraint;

    if (!var_constraint) return;

    var_data->constraint = 0;
    var_data->edit_value = 0.0f;
    remove_constraint(solver, var_constraint);
}

bool has_edit(solver_t *solver, symbol_t var) { 
    auto var_data = get_var_data(solver, var);
    return var_data->constraint; 
}

void suggest(solver_t *solver, 
        uint16_t count, const symbol_t* vars, const num_t* values) {
    for (uint16_t i = 0u; i < count; ++i) {
        symbol_t var = vars[i];
        num_t value = values[i];

        auto var_data = get_var_data(solver, var);

        if (var_data->constraint == 0) {
            enable_edit(solver, var, STRENGTH_MEDIUM);
            auto var_data = get_var_data(solver, var);
            assert(var_data->constraint);
        }
        num_t delta = value - var_data->edit_value;
        var_data->edit_value = value;
        delta_edit_constant(solver, delta, var_data->constraint);
    }
    dual_optimize(solver);
}

void suggest(solver_t *solver, symbol_t var, num_t value) {
    symbol_t vars[] = {var};
    num_t values[] = {value};
    suggest(solver, 1, vars, values);
}

}
