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
    /**
     * Allocate memory with requested size and 8 byte alignment, 
     * todo: pass alignment (for efficient ht implemenation for example)
     */
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
    uint32_t page_size;
};

/**
 * Allocate and setup solver with provided allocator or use internal default one based on malloc/free
 * @param desc solver creation info
 * @return solver instance pointer
 */
solver_t* create_solver(const solver_desc_t* desc);

/**
 * Destroy solver and free all allocated memory
 * @param solver solver
 */
void destroy_solver(solver_t* solver);

/**
 * Add variable to solver
 * @param solver solver
 * @return variable handle
 */
symbol_t create_variable(solver_t* solver);

/**
 * Remove variable from solver, expects variable is not used(poor design?)
 * @param solver solver
 * @param var variable to remove
 * @todo remove independently of referring constraints?
 */
void delete_variable(solver_t* solver, symbol_t var);

/**
 * Retrive calculated variable value
 * @param solver solver
 * @param var variable
 * @return calculated variable value
 */
num_t value(solver_t* solver, symbol_t var);

/**
 * Represents constraint declaration, defined in the next form:
 * s1 * a1 + s2 * a2 + ... + sn * an <=|==|>= c
 */
struct constraint_desc_t {
    num_t      strength;
    size_t     term_count;
    symbol_t*  symbols;
    num_t*     multipliers;
    relation_e relation;
    num_t      constant;
};

/**
 * Add new constraint to solver
 * @param solver solver
 * @param desc constraint description
 * @param[out] out_cons constraint handle
 * @return operation result
 */
result_e add_constraint(solver_t* solver, const constraint_desc_t* desc, constraint_handle_t* out_cons);

/**
 * Remove constraint out of solver
 * @param solver solver
 * @param cons constraint handle
 */
void remove_constraint(solver_t* solver, constraint_handle_t cons);

/**
 * Make variable editable
 * @param solver solver
 * @param var variable
 * @param strength strength of underlying constraint
 * @return result of adding constraint
 */
result_e enable_edit(solver_t* solver, symbol_t var, num_t strength);

/**
 * Stop editing variable
 * @param solver solver
 * @param var variable
 */
void disable_edit(solver_t* solver, symbol_t var);

/**
 * Check if variable is editable
 * @param solver solver
 * @param var variable
 * @return true if variable is editable
 */
bool has_edit(solver_t* solver, symbol_t var);

/**
 * Provide desired variable values
 * @param solver solver
 * @param count number of modified variables
 * @param vars editable symbols
 * @param values desired values
 */
void suggest(solver_t *solver, uint16_t count, const symbol_t* vars, const num_t* values);

/**
 * Provide desired value for single variable
 * @param solver solver
 * @param var editable symbol
 * @param value desired value
 */
void suggest(solver_t *solver, symbol_t var, num_t value);

}
