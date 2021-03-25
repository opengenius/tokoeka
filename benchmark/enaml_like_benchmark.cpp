#include "nanobench.h"
#include "tokoeka/solver.h"
#include <cassert>

using namespace tokoeka;

static void build_solver(solver_t* S, symbol_t width, symbol_t height)
{
    // Create custom strength
    num_t mmedium = STRENGTH_MEDIUM * 1.25;
    num_t smedium = STRENGTH_MEDIUM * 100;

    // Create the variable
    symbol_t left            = create_variable(S);
    symbol_t top             = create_variable(S);
    symbol_t contents_top    = create_variable(S);
    symbol_t contents_bottom = create_variable(S);
    symbol_t contents_left   = create_variable(S);
    symbol_t contents_right  = create_variable(S);
    symbol_t midline         = create_variable(S);
    symbol_t ctleft          = create_variable(S);
    symbol_t ctheight        = create_variable(S);
    symbol_t cttop           = create_variable(S);
    symbol_t ctwidth         = create_variable(S);
    symbol_t lb1left         = create_variable(S);
    symbol_t lb1height       = create_variable(S);
    symbol_t lb1top          = create_variable(S);
    symbol_t lb1width        = create_variable(S);
    symbol_t lb2left         = create_variable(S);
    symbol_t lb2height       = create_variable(S);
    symbol_t lb2top          = create_variable(S);
    symbol_t lb2width        = create_variable(S);
    symbol_t lb3left         = create_variable(S);
    symbol_t lb3height       = create_variable(S);
    symbol_t lb3top          = create_variable(S);
    symbol_t lb3width        = create_variable(S);
    symbol_t fl1left         = create_variable(S);
    symbol_t fl1height       = create_variable(S);
    symbol_t fl1top          = create_variable(S);
    symbol_t fl1width        = create_variable(S);
    symbol_t fl2left         = create_variable(S);
    symbol_t fl2height       = create_variable(S);
    symbol_t fl2top          = create_variable(S);
    symbol_t fl2width        = create_variable(S);
    symbol_t fl3left         = create_variable(S);
    symbol_t fl3height       = create_variable(S);
    symbol_t fl3top          = create_variable(S);
    symbol_t fl3width        = create_variable(S);

    // Add the edit variables
    enable_edit(S, width, STRENGTH_STRONG);
    enable_edit(S, height, STRENGTH_STRONG);

    // Add the constraints
    const struct {
        struct {
            symbol_t var;
            num_t    mul;
        }          term[5];
        num_t      constant;
        relation_e relation;
        num_t      strength;
    } constraints[] = {
        { {{left}},                                                -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{height}},                                              0,            relation_e::EQUAL,      STRENGTH_MEDIUM   },
        { {{top}},                                                 -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{width}},                                               -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{height}},                                              -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{top,-1},{contents_top}},                               -10,          relation_e::EQUAL,      STRENGTH_REQUIRED },
        { {{lb3height}},                                           -16,          relation_e::EQUAL,      STRENGTH_STRONG   },
        { {{lb3height}},                                           -16,          relation_e::GREATEQUAL, STRENGTH_STRONG   },
        { {{ctleft}},                                              -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{cttop}},                                               -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{ctwidth}},                                             -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{ctheight}},                                            -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{fl3left}},                                             0,            relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{ctheight}},                                            -24,          relation_e::GREATEQUAL, smedium     },
        { {{ctwidth}},                                             -1.67772e+07, relation_e::LESSEQUAL,  smedium     },
        { {{ctheight}},                                            -24,          relation_e::LESSEQUAL,  smedium     },
        { {{fl3top}},                                              -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{fl3width}},                                            -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{fl3height}},                                           -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{lb1width}},                                            -67,          relation_e::EQUAL,      STRENGTH_WEAK     },
        { {{lb2width}},                                            -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{lb2height}},                                           -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{fl2height}},                                           -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{lb3left}},                                             -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{fl2width}},                                            -125,         relation_e::GREATEQUAL, STRENGTH_STRONG   },
        { {{fl2height}},                                           -21,          relation_e::EQUAL,      STRENGTH_STRONG   },
        { {{fl2height}},                                           -21,          relation_e::GREATEQUAL, STRENGTH_STRONG   },
        { {{lb3top}},                                              -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{lb3width}},                                            -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{lb1left}},                                             -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{fl1width}},                                            -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{lb1width}},                                            -67,          relation_e::GREATEQUAL, STRENGTH_STRONG   },
        { {{fl2left}},                                             -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{lb2width}},                                            -66,          relation_e::EQUAL,      STRENGTH_WEAK     },
        { {{lb2width}},                                            -66,          relation_e::GREATEQUAL, STRENGTH_STRONG   },
        { {{lb2height}},                                           -16,          relation_e::EQUAL,      STRENGTH_STRONG   },
        { {{fl1height}},                                           -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{fl1top}},                                              -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{lb2top}},                                              -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{lb2top,-1},{lb3top},{lb2height,-1}},                   -10,          relation_e::EQUAL,      mmedium     },
        { {{lb3top,-1},{lb3height,-1},{fl3top}},                   -10,          relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{lb3top,-1},{lb3height,-1},{fl3top}},                   -10,          relation_e::EQUAL,      mmedium     },
        { {{contents_bottom},{fl3height,-1},{fl3top,-1}},          -0,           relation_e::EQUAL,      STRENGTH_MEDIUM   },
        { {{fl1top},{contents_top,-1}},                            0,            relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{fl1top},{contents_top,-1}},                            0,            relation_e::EQUAL,      mmedium     },
        { {{contents_bottom},{fl3height,-1},{fl3top,-1}},          -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{left,-1},{width,-1},{contents_right}},                 10,           relation_e::EQUAL,      STRENGTH_REQUIRED },
        { {{top,-1},{height,-1},{contents_bottom}},                10,           relation_e::EQUAL,      STRENGTH_REQUIRED },
        { {{left,-1},{contents_left}},                             -10,          relation_e::EQUAL,      STRENGTH_REQUIRED },
        { {{lb3left},{contents_left,-1}},                          0,            relation_e::EQUAL,      mmedium     },
        { {{fl1left},{midline,-1}},                                0,            relation_e::EQUAL,      STRENGTH_STRONG   },
        { {{fl2left},{midline,-1}},                                0,            relation_e::EQUAL,      STRENGTH_STRONG   },
        { {{ctleft},{midline,-1}},                                 0,            relation_e::EQUAL,      STRENGTH_STRONG   },
        { {{fl1top},{fl1height,0.5},{lb1top,-1},{lb1height,-0.5}}, 0,            relation_e::EQUAL,      STRENGTH_STRONG   },
        { {{lb1left},{contents_left,-1}},                          0,            relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{lb1left},{contents_left,-1}},                          0,            relation_e::EQUAL,      mmedium     },
        { {{lb1left,-1},{fl1left},{lb1width,-1}},                  -10,          relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{lb1left,-1},{fl1left},{lb1width,-1}},                  -10,          relation_e::EQUAL,      mmedium     },
        { {{fl1left,-1},{contents_right},{fl1width,-1}},           -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{width}},                                               0,            relation_e::EQUAL,      STRENGTH_MEDIUM   },
        { {{fl1top,-1},{fl2top},{fl1height,-1}},                   -10,          relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{fl1top,-1},{fl2top},{fl1height,-1}},                   -10,          relation_e::EQUAL,      mmedium     },
        { {{cttop},{fl2top,-1},{fl2height,-1}},                    -10,          relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{ctheight,-1},{cttop,-1},{fl3top}},                     -10,          relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{contents_bottom},{fl3height,-1},{fl3top,-1}},          -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{cttop},{fl2top,-1},{fl2height,-1}},                    -10,          relation_e::EQUAL,      mmedium     },
        { {{fl1left,-1},{contents_right},{fl1width,-1}},           -0,           relation_e::EQUAL,      mmedium     },
        { {{lb2top,-1},{lb2height,-0.5},{fl2top},{fl2height,0.5}}, 0,            relation_e::EQUAL,      STRENGTH_STRONG   },
        { {{contents_left,-1},{lb2left}},                          0,            relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{contents_left,-1},{lb2left}},                          0,            relation_e::EQUAL,      mmedium     },
        { {{fl2left},{lb2width,-1},{lb2left,-1}},                  -10,          relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{ctheight,-1},{cttop,-1},{fl3top}},                     -10,          relation_e::EQUAL,      mmedium     },
        { {{contents_bottom},{fl3height,-1},{fl3top,-1}},          -0,           relation_e::EQUAL,      STRENGTH_MEDIUM   },
        { {{lb1top}},                                              -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{lb1width}},                                            -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{lb1height}},                                           -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{fl2left},{lb2width,-1},{lb2left,-1}},                  -10,          relation_e::EQUAL,      mmedium     },
        { {{fl2left,-1},{fl2width,-1},{contents_right}},           -0,           relation_e::EQUAL,      mmedium     },
        { {{fl2left,-1},{fl2width,-1},{contents_right}},           -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{lb3left},{contents_left,-1}},                          0,            relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{lb1left}},                                             -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{ctheight,0.5},{cttop},{lb3top,-1},{lb3height,-0.5}},   0,            relation_e::EQUAL,      STRENGTH_STRONG   },
        { {{ctleft},{lb3left,-1},{lb3width,-1}},                   -10,          relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{ctwidth,-1},{ctleft,-1},{contents_right}},             -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{ctleft},{lb3left,-1},{lb3width,-1}},                   -10,          relation_e::EQUAL,      mmedium     },
        { {{fl3left},{contents_left,-1}},                          0,            relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{fl3left},{contents_left,-1}},                          0,            relation_e::EQUAL,      mmedium     },
        { {{ctwidth,-1},{ctleft,-1},{contents_right}},             -0,           relation_e::EQUAL,      mmedium     },
        { {{fl3left,-1},{contents_right},{fl3width,-1}},           -0,           relation_e::EQUAL,      mmedium     },
        { {{contents_top,-1},{lb1top}},                            0,            relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{contents_top,-1},{lb1top}},                            0,            relation_e::EQUAL,      mmedium     },
        { {{fl3left,-1},{contents_right},{fl3width,-1}},           -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{lb2top},{lb1top,-1},{lb1height,-1}},                   -10,          relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{lb2top,-1},{lb3top},{lb2height,-1}},                   -10,          relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{lb2top},{lb1top,-1},{lb1height,-1}},                   -10,          relation_e::EQUAL,      mmedium     },
        { {{fl1height}},                                           -21,          relation_e::EQUAL,      STRENGTH_STRONG   },
        { {{fl1height}},                                           -21,          relation_e::GREATEQUAL, STRENGTH_STRONG   },
        { {{lb2left}},                                             -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{lb2height}},                                           -16,          relation_e::GREATEQUAL, STRENGTH_STRONG   },
        { {{fl2top}},                                              -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{fl2width}},                                            -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{lb1height}},                                           -16,          relation_e::GREATEQUAL, STRENGTH_STRONG   },
        { {{lb1height}},                                           -16,          relation_e::EQUAL,      STRENGTH_STRONG   },
        { {{fl3width}},                                            -125,         relation_e::GREATEQUAL, STRENGTH_STRONG   },
        { {{fl3height}},                                           -21,          relation_e::EQUAL,      STRENGTH_STRONG   },
        { {{fl3height}},                                           -21,          relation_e::GREATEQUAL, STRENGTH_STRONG   },
        { {{lb3height}},                                           -0,           relation_e::GREATEQUAL, STRENGTH_REQUIRED },
        { {{ctwidth}},                                             -119,         relation_e::GREATEQUAL, smedium     },
        { {{lb3width}},                                            -24,          relation_e::EQUAL,      STRENGTH_WEAK     },
        { {{lb3width}},                                            -24,          relation_e::GREATEQUAL, STRENGTH_STRONG   },
        { {{fl1width}},                                            -125,         relation_e::GREATEQUAL, STRENGTH_STRONG   },
    };

    
    for (const auto& constraint : constraints) {
        symbol_t symbols[5];   
        num_t multipiers[5];    

        int term_count = 0;
        for (auto* p = constraint.term; p->var; ++p, ++term_count) {
            symbols[term_count] = p->var;
            multipiers[term_count] = p->mul ? p->mul : 1;
        }        

        constraint_desc_t desc = {};
        desc.strength = constraint.strength;
        desc.term_count = term_count;
        desc.symbols = symbols;
        desc.multipliers = multipiers;
        desc.relation = constraint.relation;
        desc.constant = -constraint.constant;
        
        constraint_handle_t c;
        result_e r = add_constraint(S, &desc, &c);

        assert(r == result_e::OK);
    }
}

int main()
{
    ankerl::nanobench::Bench().minEpochIterations(10).run("building solver", [&] {
        solver_desc_t solver_desc = {};
        solver_t *S = create_solver(&solver_desc);
        symbol_t width = create_variable(S);
        symbol_t height = create_variable(S);
        build_solver(S, width, height);
        ankerl::nanobench::doNotOptimizeAway(S);
        destroy_solver(S);
    });

    struct Size
    {
        int width;
        int height;
    };

    Size sizes[] = {
        { 400, 600 },
        { 600, 400 },
        { 800, 1200 },
        { 1200, 800 },
        { 400, 800 },
        { 800, 400 }
    };

    solver_desc_t solver_desc = {};
    solver_t *S = create_solver(&solver_desc);
    symbol_t widthVar = create_variable(S);
    symbol_t heightVar = create_variable(S);
    build_solver(S, widthVar, heightVar);

    for (const Size& size : sizes)
    {
        num_t width = size.width;
        num_t height = size.height;

        ankerl::nanobench::Bench().minEpochIterations(100).run("suggest value " + std::to_string(size.width) + "x" + std::to_string(size.height), [&] {
            symbol_t vars[] = {widthVar, heightVar};
            num_t values[] = {width, height};
            suggest(S, 2, vars, values);
        });
    }

    disable_edit(S, widthVar);
    disable_edit(S, heightVar);

    destroy_solver(S);

    extern uint32_t g_find_max;
    printf("\n");
    printf("g_find_max: %d\n", g_find_max);

    return 0;
}
