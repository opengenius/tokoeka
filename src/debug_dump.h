#pragma once

/*
#include "../include/tokoeka/solver.h"
#include <cstdio>

static void am_dumpkey(am_Solver *solver, am_Symbol sym) {
    int ch = 'v';
    if (is_external(solver, sym))   ch = 'v';
    else if (is_slack(solver, sym)) ch = 's';
    else if (is_error(solver, sym)) ch = 'e';
    else if (is_dummy(solver, sym)) ch = 'd';
    printf("%c%u", ch, sym);
}

static void am_dumprow(am_Solver *solver, am_Symbol row) {
    for (auto term_it = first_row_iterator(&solver->terms, row);
            term_it.term_res.term;
            term_it = next_row_iterator(&solver->terms, term_it)) {
        auto term_ptr = term_it.term_res.term;

        am_Num multiplier = term_ptr->multiplier;
        printf(" %c ", multiplier > 0.0 ? '+' : '-');
        if (multiplier < 0.0) multiplier = -multiplier;
        if (!approx(multiplier, 1.0f))
            printf("%g * ", multiplier);
         am_dumpkey(solver, term_ptr->pos.column);
    }
    printf("\n");
}

static void am_dumpsolver(am_Solver *solver) {
    int idx = 0;
    printf("-------------------------------\n");
    printf("objective: ");
    am_dumprow(solver, solver->objective);

    // printf("rows(%d):\n", (int)solver->rows.count);
    printf("rows:\n");
    for (int var_id = 1u; var_id < solver->first_unused_var_index; ++var_id) {
        if (var_id == solver->objective) continue;
        if (!has_row(&solver->terms, var_id)) continue;

        printf("%d. ", ++idx);
        am_dumpkey(solver, var_id);
        printf(" |: ");
        am_dumprow(solver, var_id);
    }
}*/
