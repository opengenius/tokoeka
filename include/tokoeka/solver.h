#pragma once

#include <cstddef>
#include <cstdint>

namespace tokoeka {

enum class result_e : uint8_t {
    OK,
    FAILED,
    UNSATISFIED,
    UNBOUND
};

enum class relation_e : uint8_t {
    LESSEQUAL,
    EQUAL,
    GREATEQUAL
};

typedef double num_t;
typedef uint16_t symbol_t;
typedef uint32_t constraint_handle_t;

struct solver_t;

struct allocator_t {
    void* (*allocate)(void *ud, size_t size);
    void (*free)(void *ud, void* p);

    void* ud;
};

const num_t STRENGTH_REQUIRED = 1000000000;
const num_t STRENGTH_STRONG   = 1000000;
const num_t STRENGTH_MEDIUM   = 1000;
const num_t STRENGTH_WEAK     = 1;

struct solver_desc_t {
    allocator_t allocator;
};

solver_t* create_solver(const solver_desc_t* desc);
void destroy_solver(solver_t* solver);

struct constraint_desc_t {
    num_t      strength;
    size_t     term_count;
    symbol_t*  symbols;
    num_t*     multipliers;
    relation_e relation;
    num_t      constant;
};

result_e add_constraint(solver_t* solver, const constraint_desc_t* desc, constraint_handle_t* out_cons);
void delete_constraint(solver_t* solver, constraint_handle_t cons);

symbol_t create_variable(solver_t* solver);
void delete_variable(solver_t* solver, symbol_t var);
num_t value(solver_t* solver, symbol_t var);

result_e edit(solver_t* solver, symbol_t var, num_t strength);
void disable_edit(solver_t* solver, symbol_t var);
bool has_edit(solver_t* solver, symbol_t var);
void suggest(solver_t *solver, uint16_t count, symbol_t* vars, num_t* values);
void suggest(solver_t *solver, symbol_t var, num_t value);

}
