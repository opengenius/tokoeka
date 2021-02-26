#include "catch2/catch.hpp"
#include "tokoeka/solver.h"

using namespace tokoeka;

TEST_CASE("simple (x==18)", "[cassowary]") {
    solver_desc_t solver_desc = {};
    solver_t *S = create_solver(&solver_desc);

    symbol_t x = create_variable(S);

    symbol_t symbols[] = {x};
    num_t multipiers[] = {1.0f};

    constraint_desc_t desc = {};
    desc.strength = STRENGTH_REQUIRED;
    desc.term_count = 1;
    desc.symbols = symbols;
    desc.multipliers = multipiers;
    desc.relation = relation_e::EQUAL;
    desc.constant = 18.0f;

    constraint_handle_t c;
    result_e r = add_constraint(S, &desc, &c);

    REQUIRE(r == result_e::OK);
    REQUIRE(value(S, x) == 18.0f);

    destroy_solver(S);
}

TEST_CASE("2 vars, 2 constraints", "[cassowary]") {
    solver_desc_t solver_desc = {};
    solver_t *S = create_solver(&solver_desc);

    symbol_t x = create_variable(S);
    symbol_t y = create_variable(S);

    // x == 20
    {
        symbol_t symbols[] = {x};
        num_t multipiers[] = {1.0f};

        constraint_desc_t desc = {};
        desc.strength = STRENGTH_REQUIRED;
        desc.term_count = 1;
        desc.symbols = symbols;
        desc.multipliers = multipiers;
        desc.relation = relation_e::EQUAL;
        desc.constant = 20.0f;

        constraint_handle_t c;
        result_e r = add_constraint(S, &desc, &c);
        REQUIRE(r == result_e::OK);
    }

    //x == y + 8
    {
        symbol_t symbols[] = {x,    y};
        num_t multipiers[] = {1.0f, -1.0f};

        constraint_desc_t desc = {};
        desc.strength = STRENGTH_REQUIRED;
        desc.term_count = 2;
        desc.symbols = symbols;
        desc.multipliers = multipiers;
        desc.relation = relation_e::EQUAL;
        desc.constant = 8.0f;

        constraint_handle_t c;
        result_e r = add_constraint(S, &desc, &c);
        REQUIRE(r == result_e::OK);
    }

    REQUIRE(value(S, x) == 20.0f);
    REQUIRE(value(S, y) == 12.0f);

    destroy_solver(S);
}

TEST_CASE("weak strength", "[cassowary]") {
    solver_desc_t solver_desc = {};
    solver_t *S = create_solver(&solver_desc);

    symbol_t x = create_variable(S);
    symbol_t y = create_variable(S);

    // x <= y
    {
        symbol_t symbols[] = {x,    y};
        num_t multipiers[] = {1.0f, -1.0f};

        constraint_desc_t desc = {};
        desc.strength = STRENGTH_REQUIRED;
        desc.term_count = 2;
        desc.symbols = symbols;
        desc.multipliers = multipiers;
        desc.relation = relation_e::LESSEQUAL;

        constraint_handle_t c;
        result_e r = add_constraint(S, &desc, &c);
        REQUIRE(r == result_e::OK);
    }

    // y == x + 3.0
    {
        symbol_t symbols[] = {x,    y};
        num_t multipiers[] = {-1.0f, 1.0f};

        constraint_desc_t desc = {};
        desc.strength = STRENGTH_REQUIRED;
        desc.term_count = 2;
        desc.symbols = symbols;
        desc.multipliers = multipiers;
        desc.relation = relation_e::EQUAL;
        desc.constant = 3.0f;

        constraint_handle_t c;
        result_e r = add_constraint(S, &desc, &c);
        REQUIRE(r == result_e::OK);
    }
    // x == 10.0
    {
        symbol_t symbols[] = {x};
        num_t multipiers[] = {1.0f};

        constraint_desc_t desc = {};
        desc.strength = STRENGTH_WEAK;
        desc.term_count = 1;
        desc.symbols = symbols;
        desc.multipliers = multipiers;
        desc.relation = relation_e::EQUAL;
        desc.constant = 10.0f;

        constraint_handle_t c;
        result_e r = add_constraint(S, &desc, &c);
        REQUIRE(r == result_e::OK);
    }
    // y == 10.0
    {
        symbol_t symbols[] = {y};
        num_t multipiers[] = {1.0f};

        constraint_desc_t desc = {};
        desc.strength = STRENGTH_WEAK;
        desc.term_count = 1;
        desc.symbols = symbols;
        desc.multipliers = multipiers;
        desc.relation = relation_e::EQUAL;
        desc.constant = 10.0f;

        constraint_handle_t c;
        result_e r = add_constraint(S, &desc, &c);
        REQUIRE(r == result_e::OK);
    }

    // add if for 7/10 variant (same strength)?
    REQUIRE(value(S, x) == 10.0f);
    REQUIRE(value(S, y) == 13.0f);

    destroy_solver(S);
}

TEST_CASE("edit variable", "[cassowary]") {
    solver_desc_t solver_desc = {};
    solver_t *S = create_solver(&solver_desc);

    symbol_t left = create_variable(S);
    symbol_t mid = create_variable(S);
    symbol_t right = create_variable(S);

    // mid == (left + right) / 2
    {
        symbol_t symbols[] = {mid,  left,  right};
        num_t multipiers[] = {1.0f, -0.5f, -0.5f};

        constraint_desc_t desc = {};
        desc.strength = STRENGTH_REQUIRED;
        desc.term_count = 3;
        desc.symbols = symbols;
        desc.multipliers = multipiers;
        desc.relation = relation_e::EQUAL;

        constraint_handle_t c;
        result_e r = add_constraint(S, &desc, &c);
        REQUIRE(r == result_e::OK);
    }

    // right == left + 10
    {
        symbol_t symbols[] = {left,  right};
        num_t multipiers[] = {-1.0f, 1.0f};

        constraint_desc_t desc = {};
        desc.strength = STRENGTH_REQUIRED;
        desc.term_count = 2;
        desc.symbols = symbols;
        desc.multipliers = multipiers;
        desc.relation = relation_e::EQUAL;
        desc.constant = 10.0f;

        constraint_handle_t c;
        result_e r = add_constraint(S, &desc, &c);
        REQUIRE(r == result_e::OK);
    }
    // right <= 100
    {
        symbol_t symbols[] = {right};
        num_t multipiers[] = {1.0f};

        constraint_desc_t desc = {};
        desc.strength = STRENGTH_REQUIRED;
        desc.term_count = 1;
        desc.symbols = symbols;
        desc.multipliers = multipiers;
        desc.relation = relation_e::LESSEQUAL;
        desc.constant = 100.0f;

        constraint_handle_t c;
        result_e r = add_constraint(S, &desc, &c);
        REQUIRE(r == result_e::OK);
    }
    // left >= 0
    {
        symbol_t symbols[] = {left};
        num_t multipiers[] = {1.0f};

        constraint_desc_t desc = {};
        desc.strength = STRENGTH_REQUIRED;
        desc.term_count = 1;
        desc.symbols = symbols;
        desc.multipliers = multipiers;
        desc.relation = relation_e::GREATEQUAL;
        desc.constant = 0.0f;

        constraint_handle_t c;
        result_e r = add_constraint(S, &desc, &c);
        REQUIRE(r == result_e::OK);
    }

    REQUIRE(value(S, left) == 90.0f);
    REQUIRE(value(S, mid) == 95.0f);
    REQUIRE(value(S, right) == 100.0f);

    edit(S, mid, STRENGTH_STRONG);
    suggest(S, mid, 3.);

    REQUIRE(value(S, left) == 0.0f);
    REQUIRE(value(S, mid) == 5.0f);
    REQUIRE(value(S, right) == 10.0f);

    destroy_solver(S);
}

TEST_CASE("match heights", "[cassowary]") {
    solver_desc_t solver_desc = {};
    solver_t *S = create_solver(&solver_desc);

    struct Constrainable {
        symbol_t top;
        symbol_t height;

        void init(solver_t *S) {
            top = create_variable(S);
            height = create_variable(S);
        }
    };

    Constrainable parent = {};
    parent.init(S);

    Constrainable child = {};
    child.init(S);

    // child.top == parent.top
    {
        symbol_t symbols[] = {child.top,  parent.top};
        num_t multipiers[] = {1.0f, -1.0f};

        constraint_desc_t desc = {};
        desc.strength = STRENGTH_REQUIRED;
        desc.term_count = 2;
        desc.symbols = symbols;
        desc.multipliers = multipiers;
        desc.relation = relation_e::EQUAL;

        constraint_handle_t c;
        result_e r = add_constraint(S, &desc, &c);
        REQUIRE(r == result_e::OK);
    }

    // child.bottom == parent.bottom
    {
        symbol_t symbols[] = {child.top, child.height, parent.top, parent.height};
        num_t multipiers[] = {1.0f,      1.0f,         -1.0f,      -1.0f};

        constraint_desc_t desc = {};
        desc.strength = STRENGTH_REQUIRED;
        desc.term_count = 4;
        desc.symbols = symbols;
        desc.multipliers = multipiers;
        desc.relation = relation_e::EQUAL;

        constraint_handle_t c;
        result_e r = add_constraint(S, &desc, &c);
        REQUIRE(r == result_e::OK);
    }

    edit(S, child.height, STRENGTH_STRONG);
    suggest(S, child.height, 24.);

    REQUIRE(value(S, parent.height) == 24.0f);

    destroy_solver(S);
}

// delete constraint test
// inconsistent constraints