#include "tokoeka/solver.h"

#include <cassert>
#include <type_traits>
#include <cfloat>
#include <cstdlib>
#include <cstring>
#include "hash_table.h"

namespace tokoeka {

static_assert(std::is_same<num_t, double>::value, "");
const num_t NUM_MAX = DBL_MAX;
const num_t NUM_EPS = 1e-6;
const uint32_t FREELIST_INDEX = 0u;

enum class symbol_type_e : uint8_t {
    EXTERNAL,
    SLACK,
    ERROR,
    DUMMY
};

typedef struct {
    symbol_type_e       type;
    constraint_handle_t constraint;
    num_t               edit_value;
} var_data_t;

typedef union
{
    var_data_t var;
    uint32_t   next; // free list
} var_entry_t;

typedef struct {
    symbol_t  marker;
    symbol_t  other; // nullable
    num_t     strength;
} constraint_data_t;

typedef union
{
    constraint_data_t constraint;
    uint32_t          next; // free list
} constraint_entry_t;

typedef struct {
    symbol_t row, column;
} term_coord_t;

typedef struct {
    term_coord_t pos;
    symbol_t     prev_row, next_row;
    symbol_t     prev_column, next_column;
    // padding: 4 bytes
    num_t        multiplier;
} term_data_t;

typedef struct {
    void*  data_buf;
    size_t size; // max entries (size * entry_size bytes)
    size_t entry_size;
} array_t;

typedef struct {
    term_data_t* data;
    uint32_t     size;
    uint32_t     count;
} terms_table_t;

struct solver_t {
    allocator_t allocator;

    array_t vars; // var_data_t array
    uint32_t first_unused_var_index;

    array_t constraints; // constraint_data_t array
    uint32_t first_unused_constraint_index;

    terms_table_t terms;
    symbol_t objective;
    symbol_t infeasible_rows; // use next constant term row links for infeasible rows
};


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

/* array */

static void init_array(array_t *arr, size_t entry_size) {
    arr->entry_size = entry_size; 
}

static void free_array(allocator_t* alloc, array_t *arr) {
    size_t size = arr->size * arr->entry_size;
    if (size) free(alloc, arr->data_buf);
    init_array(arr, arr->entry_size);
}

static size_t array_size(const array_t *arr) {
    return arr->size;
}

static void* array_get(array_t *arr, size_t position) {
    assert(arr->data_buf);
    assert(position < arr->size);

    size_t offset = position * arr->entry_size;
    return &((char*)arr->data_buf)[offset];
}

static void array_grow(allocator_t* alloc, array_t *arr, size_t new_size) {
    size_t new_size_in_bytes = new_size * arr->entry_size;
    void* new_buf = allocate(alloc, new_size_in_bytes);
    if (arr->data_buf) {
        memcpy(new_buf, arr->data_buf, new_size_in_bytes);
        free(alloc, arr->data_buf);
    }
    arr->data_buf = new_buf;
    arr->size = new_size;
}

///////////////////////////////////////////////////////////////////////////////
// Term hash table 
///////////////////////////////////////////////////////////////////////////////

static uint32_t xorshift(uint32_t n, int i) {
  return n ^ (n >> i);
}

static uint32_t distribute(const uint32_t& n) {
  uint32_t p = 0x55555555ul; // pattern of alternating 0 and 1
  uint32_t c = 3423571495ul; // random uneven integer constant; 
  return c * xorshift(p * xorshift(n, 16), 16);
}

static uint32_t hash_uint32_t(const term_coord_t& pos) {
    uint32_t combined = (pos.row << 16) | pos.column;
    return distribute(combined);
}

static uint32_t element_data_hash(const void* key) {
    auto key_pos = (const term_coord_t*)key;

    return hash_uint32_t(*key_pos);
}

static uint32_t element_data_hash_index(void* ht_data, uint32_t index) {
    auto element_array = (const term_data_t*)ht_data;

    return hash_uint32_t(element_array[index].pos);
}

static bool element_data_key_equal(void* ht_data, uint32_t index, const void* key) {
    auto element_array = (const term_data_t*)ht_data;
    auto key_pos = (const term_coord_t*)key;

    auto& pos_at_index = element_array[index].pos;
    return pos_at_index.row == key_pos->row && pos_at_index.column == key_pos->column;
}

static bool element_data_key_valid(void* ht_data, uint32_t index) { 
    auto element_array = (const term_data_t*)ht_data;

    auto& pos_at_index = element_array[index].pos;
    return pos_at_index.row || pos_at_index.column;
}

static void element_data_move(void* ht_data, uint32_t dst_index, uint32_t src_index) {
    auto element_array = (term_data_t*)ht_data;

    element_array[dst_index] = element_array[src_index];
}

static void element_data_reset(void* ht_data, uint32_t index) {
    auto element_array = (term_data_t*)ht_data;

    auto& pos_at_index = element_array[index].pos;
    pos_at_index.row = 0u;
    pos_at_index.column = 0u;
}

static const hash_array_protocol_t s_term_ht_impl {
    element_data_hash,
    element_data_hash_index,
    element_data_key_equal,
    element_data_key_valid,
    element_data_move,
    element_data_reset
};

///////////////////////////////////////////////////////////////////////////////
// Linear equation tableau (sparse matrix in DOK with row, column linked lists)
///////////////////////////////////////////////////////////////////////////////

static void init_table(allocator_t* alloc, terms_table_t* terms, uint32_t size) {
    auto buffer_byte_size = sizeof(term_data_t) * size;
    terms->data = (term_data_t*)allocate(alloc, buffer_byte_size);
    terms->size = size;
    memset(terms->data, 0, buffer_byte_size);
}

static void free_table(allocator_t* alloc, terms_table_t* terms) {
    free(alloc, terms->data);
}

static term_data_t* get_term_no_assert(terms_table_t* terms, const term_coord_t& coord) {
    hash_desc_t ht_desc = {};
    ht_desc.ht_api = &s_term_ht_impl;
    ht_desc.data = terms->data;
    ht_desc.element_count = terms->size;

    auto index = hash_find_index(&ht_desc, &coord);
    term_data_t* res = &terms->data[index];
    return res;
}

static term_data_t* get_term(terms_table_t* terms, const term_coord_t& coord) {
    term_data_t* res = get_term_no_assert(terms, coord);
    assert(res->pos.row == coord.row && res->pos.column == coord.column);
    return res;
}

static void table_grow_rehash(allocator_t* alloc, terms_table_t* terms, uint32_t size) {
    terms_table_t new_table = {};
    init_table(alloc, &new_table, size);

    for (size_t i = 0u; i < terms->size; ++i) {
        auto& src_term = terms->data[i];
        if (!src_term.pos.row && !src_term.pos.column) continue;

        auto dst_term = get_term_no_assert(&new_table, src_term.pos);
        *dst_term = src_term;
    }
    new_table.count = terms->count;

    free_table(alloc, terms);
    *terms = new_table;
}

typedef struct {
    term_data_t* term;
    uint32_t     index;
} term_result_t;

static term_result_t find_term(terms_table_t* terms, const term_coord_t& coord) {
    term_result_t res = {};

    hash_desc_t ht_desc = {};
    ht_desc.ht_api = &s_term_ht_impl;
    ht_desc.data = terms->data;
    ht_desc.element_count = terms->size;

    res.index = hash_find_index(&ht_desc, &coord);
    if (res.index != ~0u) {
        res.term = &terms->data[res.index];
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

    auto term = get_term(terms, {sym_list->next_row, sym});

    res.term_res = {term, (uint32_t)(term - terms->data)};
    res.next_coord = {term->next_row, sym};

    return res;
}

static term_iterator_t next_symbol_iterator(terms_table_t* terms, const term_iterator_t& iter) {
    term_iterator_t res = {};

    if (!iter.next_coord.row) return res;

    auto term_it = find_term(terms, iter.next_coord);
    assert(term_it.term->pos.row == iter.next_coord.row && 
            term_it.term->pos.column == iter.next_coord.column && "symbol list's corrupted");

    res.term_res = term_it;
    res.next_coord = {term_it.term->next_row, iter.next_coord.column};

    return res;
}

typedef struct {
    term_result_t term_res;
    term_coord_t next_coord;
} term_row_iterator_t;

static term_row_iterator_t first_row_iterator(terms_table_t* terms, symbol_t row) {
    term_row_iterator_t res = {};

    auto sym_list = find_term(terms, {row, 0u});
    assert(sym_list.term->pos.row == row && sym_list.term->pos.column == 0u && "row's corrupted");

    res.term_res = sym_list;
    res.next_coord = {row, sym_list.term->next_column};

    return res;
}

static term_row_iterator_t next_row_iterator(terms_table_t* terms, const term_row_iterator_t& iter) {
    term_row_iterator_t res = {};

    if (!iter.next_coord.column) return res;

    auto term_it = find_term(terms, iter.next_coord);
    assert(term_it.term->pos.row == iter.next_coord.row && 
            term_it.term->pos.column == iter.next_coord.column && "row's corrupted");

    res.term_res = term_it;
    res.next_coord = {iter.next_coord.row, term_it.term->next_column};

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

    assert(terms->count);
    --terms->count;

    hash_desc_t ht_desc = {};
    ht_desc.ht_api = &s_term_ht_impl;
    ht_desc.data = terms->data;
    ht_desc.element_count = terms->size;
    hash_erase(&ht_desc, term_it->index);
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

static void add_term(terms_table_t* terms, symbol_t row, symbol_t sym, num_t value) {
    const term_coord_t key = {row, sym};
    auto var_term_it = find_term(terms, key);
    assert(var_term_it.index != ~0u && "expect ht to grow?");

    if (var_term_it.term->pos.row != key.row || 
        var_term_it.term->pos.column != key.column) {
        // no var, add
        // todo: handle adding to full table
        assert(terms->count < terms->size);
        ++terms->count;

        term_data_t new_term = {};
        new_term.pos = key;
        if (row && sym) {
            // todo: cache row terms for multiple add_term calls for single row
            link_term(terms, key, new_term);
        }

        *var_term_it.term = new_term;
    }

    var_term_it.term->multiplier += value;
    if (row && sym && near_zero(var_term_it.term->multiplier)) {
        // delete key
        delete_term(terms, &var_term_it);
    }
}

static void add_row(terms_table_t* terms, symbol_t row, symbol_t other, num_t multiplier) {
    for (auto term_it = first_row_iterator(terms, other); 
            term_it.term_res.term;
            term_it = next_row_iterator(terms, term_it)) {
        auto term_ptr = term_it.term_res.term;

        add_term(terms, row, term_ptr->pos.column, term_ptr->multiplier * multiplier);
    }
}

static bool has_row(terms_table_t* terms, symbol_t row) {
    return find_existing_term(terms, {row, 0u}) != nullptr;
}

static void merge_row(terms_table_t* terms, symbol_t row, symbol_t var, num_t multiplier) {
    if (has_row(terms, var))
        add_row(terms, row, var, multiplier);
    else
        add_term(terms, row, var, multiplier);
}

static void init_row(terms_table_t* terms, symbol_t row, num_t constant) {
    assert(!has_row(terms, row));

    // row is defined with constant term
    add_term(terms, row, 0u, constant);
}

///////////////////////////////////////////////////////////////////////////////
// Solver implementation
///////////////////////////////////////////////////////////////////////////////

static var_data_t* get_var_data(solver_t *solver, symbol_t var) {
    assert(solver);
    assert(var);

    auto var_data = (var_entry_t*)array_get(&solver->vars, var);
    return &var_data->var;
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
    symbol_t id = {};

    // try free list
    auto free_list_head = (var_entry_t*)array_get(&solver->vars, FREELIST_INDEX); 
    if (free_list_head->next) {
        id = free_list_head->next;
        auto el = (var_entry_t*)array_get(&solver->vars, id); 
        free_list_head->next = el->next;

    // try unused elements
    } else if (solver->first_unused_var_index < array_size(&solver->vars)) {
        id = solver->first_unused_var_index++;

    // grow
    } else {
        auto new_size = array_size(&solver->vars) * 2;
        array_grow(&solver->allocator, &solver->vars, new_size);

        id = solver->first_unused_var_index++;
    }

    static const var_data_t init_data = {};
    auto var = get_var_data(solver, id);
    *var = init_data;
    var->type = type;

    // init symbol link list
    add_term(&solver->terms, 0u, id, 0.0f);

    return id;
}

static constraint_data_t* constraint_data(solver_t *solver, constraint_handle_t cons_id) {
    auto cons_entry = (constraint_entry_t*)array_get(&solver->constraints, cons_id);
    return &cons_entry->constraint;
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
    if (solver->terms.size < solver->terms.count * 2) {
        table_grow_rehash(&solver->allocator, &solver->terms, solver->terms.size * 2);
    }

    term_coord_t key = {row, entry};
    auto term_it = find_term(&solver->terms, key);
    num_t reciprocal = 1.0f / term_it.term->multiplier;
    assert(entry != exit && !near_zero(term_it.term->multiplier));
    delete_term(&solver->terms, &term_it);

    add_row(&solver->terms, entry, row, -reciprocal);
    free_row(&solver->terms, row);
    if (row != exit) delete_variable(solver, row);

    if (exit != 0) add_term(&solver->terms, entry, exit, reciprocal);

    for (auto sym_iter = first_symbol_iterator(&solver->terms, entry); 
            sym_iter.term_res.term; 
            sym_iter = next_symbol_iterator(&solver->terms, sym_iter) ) {

        symbol_t it_row = sym_iter.term_res.term->pos.row;
        auto term_multiplier = sym_iter.term_res.term->multiplier;

        // substitute entry term with solved row
        delete_term(&solver->terms, &sym_iter.term_res, unlink_frags_e::ROW);
        add_row(&solver->terms, it_row, entry, term_multiplier);
        
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
    init_row(&solver->terms, row, -desc->constant);
    for (size_t i = 0; i < desc->term_count; ++i) {
        merge_row(&solver->terms, row, desc->symbols[i], desc->multipliers[i]);
    }

    if (desc->relation != relation_e::EQUAL) {
        num_t coeff = desc->relation == relation_e::LESSEQUAL ? 1.0f : -1.0f;
        cons->marker = new_symbol(solver, symbol_type_e::SLACK);
        add_term(&solver->terms, row, cons->marker, coeff);
        if (cons->strength < STRENGTH_REQUIRED) {
            cons->other = new_symbol(solver, symbol_type_e::ERROR);
            add_term(&solver->terms, row, cons->other, -coeff);
            add_term(&solver->terms, solver->objective, cons->other, cons->strength);
        }
    } else if (cons->strength >= STRENGTH_REQUIRED) {
        cons->marker = new_symbol(solver, symbol_type_e::DUMMY);
        add_term(&solver->terms, row, cons->marker, 1.0f);
    } else {
        cons->marker = new_symbol(solver, symbol_type_e::ERROR);
        cons->other = new_symbol(solver, symbol_type_e::ERROR);
        add_term(&solver->terms, row, cons->marker, -1.0f);
        add_term(&solver->terms, row, cons->other,   1.0f);
        add_term(&solver->terms, solver->objective, cons->marker, cons->strength);
        add_term(&solver->terms, solver->objective, cons->other,  cons->strength);
    }
    if (value(solver, row) < 0.0f) multiply_row(&solver->terms, row, -1.0f);
    return row;
}

static void remove_errors(solver_t *solver, constraint_data_t *cons) {
    if (is_error(solver, cons->marker))
        merge_row(&solver->terms, solver->objective, cons->marker, -cons->strength);
    if (cons->other && is_error(solver, cons->other))
        merge_row(&solver->terms, solver->objective, cons->other, -cons->strength);
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

    optimize(solver, solver->objective);

    delete_variable(solver, cons_data->marker);
    delete_variable(solver, cons_data->other);
}

static result_e add_with_artificial(solver_t *solver, symbol_t row) {
    symbol_t a = new_symbol(solver, symbol_type_e::SLACK); /* artificial variable will be removed */
    add_row(&solver->terms, a, row, 1.0f);

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

static result_e try_addrow(solver_t *solver, symbol_t row, const constraint_data_t *cons) {
    symbol_t subject = 0u;

    bool all_terms_dummy = true;
    for (auto term_it = first_row_term_iterator(&solver->terms, row);
            term_it.term_res.term;
            term_it = next_row_iterator(&solver->terms, term_it)) {
        auto term_ptr = term_it.term_res.term;

        auto term_key = term_ptr->pos.column;
        if (is_external(solver, term_key)) { 
            subject = term_key; 
            break; 
        }

        all_terms_dummy = all_terms_dummy && is_dummy(solver, term_key);
    }

    if (!subject && is_pivotable(solver, cons->marker)) {
        term_data_t *mterm = get_term(&solver->terms, {row, cons->marker});
        if (mterm->multiplier < 0.0f) subject = cons->marker;
    }
    if (!subject && cons->other && is_pivotable(solver, cons->other)) {
        term_data_t *mterm = get_term(&solver->terms, {row, cons->other});
        if (mterm->multiplier < 0.0f) subject = cons->other;
    }
    if (!subject && all_terms_dummy) {
        if (near_zero(value(solver, row)))
            subject = cons->marker;
        else {
            free_row(&solver->terms, row);
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
    const int PAGE_SIZE = 4096; // todo: pass as input
    init_array(&solver->vars, sizeof(var_entry_t));
    array_grow(&solver->allocator, &solver->vars, PAGE_SIZE / sizeof(var_entry_t));
    auto free_list_head = (var_entry_t*)array_get(&solver->vars, FREELIST_INDEX);
    free_list_head->next = 0u;
    ++solver->first_unused_var_index; // reserve 0 for invalid index, 0 is used as free list head

    init_array(&solver->constraints, sizeof(constraint_entry_t));
    array_grow(&solver->allocator, &solver->constraints, PAGE_SIZE / sizeof(constraint_entry_t));
    auto cons_free_list_head = (constraint_entry_t*)array_get(&solver->constraints, FREELIST_INDEX);
    cons_free_list_head->next = 0u;
    ++solver->first_unused_constraint_index; // reserve 0 for invalid index, 0 is used as free list head

    init_table(&solver->allocator, &solver->terms, PAGE_SIZE / sizeof(term_data_t));// *16
    
    // init objective function
    solver->objective = new_symbol(solver, symbol_type_e::EXTERNAL);
    init_row(&solver->terms, solver->objective, 0.0f);

    return solver;
}

void destroy_solver(solver_t *solver) {
    assert(solver);

    free_array(&solver->allocator, &solver->vars);
    free_array(&solver->allocator, &solver->constraints);
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

    auto var_data = (var_entry_t*)array_get(&solver->vars, var); 
    delete_constraint(solver, var_data->var.constraint);

    // todo: delete rows? 
    assert(!has_row(&solver->terms, var));
    assert(!first_symbol_iterator(&solver->terms, var).term_res.term);

    // delete symbol link list
    auto term_it = find_term(&solver->terms, {0u, var});
    delete_term(&solver->terms, &term_it, unlink_frags_e::NONE);

    // link to free list
    auto free_list_head = (var_entry_t*)array_get(&solver->vars, FREELIST_INDEX);
    var_data->next = free_list_head->next;
    free_list_head->next = var;
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

    int id = 0;
    // try free list
    auto free_list_head = (constraint_entry_t*)array_get(&solver->constraints, FREELIST_INDEX); 
    if (free_list_head->next) {
        id = free_list_head->next;
        auto el = (constraint_entry_t*)array_get(&solver->constraints, id); 
        free_list_head->next = el->next;

    // try unused elements
    } else if (solver->first_unused_constraint_index < array_size(&solver->constraints)) {
        id = solver->first_unused_constraint_index++;

    // grow
    } else {
        auto new_size = array_size(&solver->constraints) * 2;
        array_grow(&solver->allocator, &solver->constraints, new_size);

        id = solver->first_unused_constraint_index++;
    }

    *constraint_data(solver, id) = cons_data;
    *out_cons = id;

    assert(solver->infeasible_rows == 0);
    return ret;
}

void delete_constraint(solver_t *solver, constraint_handle_t cons) {
    assert(solver);
    
    if (!cons) return;

    remove_vars(solver, cons);

    // link to free list
    auto free_list_head = (constraint_entry_t*)array_get(&solver->constraints, FREELIST_INDEX);
    auto entry = (constraint_entry_t*)array_get(&solver->constraints, cons); 
    entry->next = free_list_head->next;
    free_list_head->next = cons;
}

result_e edit(solver_t *solver, symbol_t var, num_t strength) {
    if (strength >= STRENGTH_STRONG) strength = STRENGTH_STRONG;

    auto var_data = get_var_data(solver, var);
    if (var_data->constraint) {
        delete_constraint(solver, var_data->constraint);
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
    if (add_constraint(solver, &desc, &cons) != result_e::OK) assert(0); // todo

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
    delete_constraint(solver, var_constraint);
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
            edit(solver, var, STRENGTH_MEDIUM);
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
