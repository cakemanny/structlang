#include "reg_alloc.h"
#include "liveness.h"
#include <assert.h>
#include "list.h"
#include "mem.h"

#define var __auto_type

#define BitsetLen(len) (((len) + 63) / 64)
#define IsBitSet(x, i) (( (x)[(i)>>6] & (1ULL<<((i)&63)) ) != 0ULL)
#define SetBit(x, i) (x)[(i)>>6] |= (1ULL<<((i)&63))
#define ClearBit(x, i) (x)[(i)>>6] &= (1ULL<<((i)&63)) ^ 0xFFFFFFFFFFFFFFFFULL

/*
 * Holds all of our worklists and state, etc for the graph colouring
 * algorithm.
 */
typedef struct reg_alloc_info_t {

    const int K; // The number of registers on the machine

    lv_node_list_t* precolored;
    lv_node_list_t* initial;
    lv_node_list_t* simplify_worklist;
    lv_node_list_t* spill_worklist;
    lv_node_list_t* spilled_nodes;

    lv_node_list_t* colored_nodes;
    lv_node_list_t* select_stack;

    int* degree; // an array containing the degree of each node.
    int* color; // colours assigned to the node with ID idx;
    lv_node_list_t** adj_list; // a list of non-precoloured adjacents

    lv_flowgraph_t* flowgraph;

} reg_alloc_info_t;

// if we haven't implemented coalescing then
// def get_alias(n): return n


/*
 * node_list_remove removes the list cell from the worklist, for the
 * node. it then returns it to the caller.
 */
static lv_node_list_t*
node_list_remove(lv_node_list_t** pworklist, const lv_node_t* node)
{
    var p = pworklist;
    for (; *p; p = &((*p)->nl_list)) {
        if (lv_eq((*p)->nl_node, node)) {

            var found = *p;
            // unchain the cell we found
            *p = found->nl_list;
            // for sanity, ensure our cell is detached
            found->nl_list = NULL;
            return found;
        }
    }
    assert(!"not found");
}

static bool
node_list_contains(const lv_node_list_t* haystack, const lv_node_t* node)
{
    for (var h = haystack; h; h = h->nl_list) {
        if (lv_eq(h->nl_node, node)) {
            return true;
        }
    }
    return false;
}


static void
node_list_free(lv_node_list_t** pnode_list)
{
    while (*pnode_list) {
        var to_free = *pnode_list;
        *pnode_list = (*pnode_list)->nl_list;
        free(to_free);
    }
}

/*
 * These are used when we have Tables with temp_t's as keys
 */
static int cmptemp(const void* x, const void* y)
{
    const temp_t* xx = x;
    const temp_t* yy = y;
    return xx->temp_id - yy->temp_id;
}
static unsigned hashtemp(const void* key)
{
    const temp_t* k = key;
    return k->temp_id;
}

/*
 *
 */
static void
decrement_degree(
        reg_alloc_info_t* info,
        lv_node_t* m)
{
    int d = info->degree[m->lvn_idx];
    info->degree[m->lvn_idx] = d - 1;
    if (d == info->K) {
        // If we were to implement coalescing, then there would be
        // something about enabling moves, and possibly adding to the
        // freeze worklist.

        // instead we move it from the spill worklist to the simplify worklist
        var m_cell = node_list_remove(&(info->spill_worklist), m);
        m_cell->nl_list = info->simplify_worklist;
        info->simplify_worklist = m_cell;
    }
}

/*
 * Simplify takes a node, n, from the worklist, pushes it onto
 * the select stack and decrements the degree of each node
 * in the adjacent set of n
 */
static void
simplify(
        reg_alloc_info_t* info) {
    // Remove node from simplifyWorklist
    // We take the first cell of the list and cons
    // it onto the selectStack instead of actually pulling out the node
    assert(info->simplify_worklist != NULL);

    var n_cell = info->simplify_worklist;
    info->simplify_worklist = info->simplify_worklist->nl_list;

    // push onto the select_stack
    n_cell->nl_list = info->select_stack;
    info->select_stack = n_cell;


    var node = n_cell->nl_node;

    for (var s = info->adj_list[node->lvn_idx]; s; s = s->nl_list) {
        var m = s->nl_node;
        decrement_degree(info, m);
    }
}

/*
 * We use a spill cost that is the number of uses and defs in the flow
 * graph.
 */
static int
spill_cost(
        reg_alloc_info_t* info,
        lv_node_t* node) {

    var flow = info->flowgraph;
    temp_list_t* use_n = Table_get(flow->lvfg_use, node);
    temp_list_t* def_n = Table_get(flow->lvfg_def, node);

    return list_length(use_n) + list_length(def_n);
}


static void
select_spill(
        reg_alloc_info_t* info) {
    // 1. select m from spill_worklist
    // 2. remove m from spill_worklist
    // 3. push m onto simplify_worklist

    // Find the node with the least spill cost
    var m = info->spill_worklist->nl_node;
    var cost = spill_cost(info, m);
    for (var c = info->spill_worklist->nl_list; c; c = c->nl_list) {
        var this_cost = spill_cost(info, c->nl_node);
        if (this_cost < cost) {
            cost = this_cost;
            m = c->nl_node;
        }
    }

    var m_cell = node_list_remove(&(info->spill_worklist), m);
    m_cell->nl_list = info->simplify_worklist;
    info->simplify_worklist = m_cell;
}


static void
assign_colors(
        reg_alloc_info_t* info) {

    while (info->select_stack != NULL) {
        // pop n from select_stack
        var n_cell = info->select_stack;
        info->select_stack = info->select_stack->nl_list;
        var node = n_cell->nl_node;

        assert(info->K <= 64);
        uint64_t _ok_colors = 0;
        uint64_t* ok_colors = &_ok_colors;
        for (int i = 0; i < info->K; i++) {
            SetBit(ok_colors, i);
        }

        // Remove colors which are already used by adjacent nodes.
        lv_node_list_t* adj = info->adj_list[node->lvn_idx];
        for (var w = adj; w; w = w->nl_list) {
            if (node_list_contains(info->colored_nodes, w->nl_node)
                    || node_list_contains(info->precolored, w->nl_node)) {
                var w_color = info->color[w->nl_node->lvn_idx];
                // remove from ok_colors
                ClearBit(ok_colors, w_color);
            }
        }
        // If we have no remaining colours, spill.
        if (__builtin_popcountll(_ok_colors) == 0) {
            n_cell->nl_list = info->spilled_nodes;
            info->spilled_nodes = n_cell;
        } else {
            // add n to coloured nodes
            n_cell->nl_list = info->colored_nodes;
            info->colored_nodes = n_cell;

            // store the new first available colour for n
            int new_color = __builtin_ctzll(_ok_colors);
            info->color[node->lvn_idx] = new_color;
        }
    }
}


static
struct ra_color_result {
    Table_T racr_allocation; // temp_t* -> register (char*)
    temp_list_t* racr_spills; // a list of spills
} ra_color(
    lv_igraph_t* interference,
    lv_flowgraph_t* flowgraph,
    Table_T initial_allocation, // temp_t* -> register (char*)
    /* registers is just a list of all machine registers */
    const char* registers[]) // maybe this could be an array?
{
    // Prepare
    reg_alloc_info_t info = {
        .K = Table_length(initial_allocation),
        .flowgraph = flowgraph,
    };

    var nodes = lv_nodes(interference->lvig_graph);
    var count_nodes = Table_length(interference->lvig_gtemp);
    info.degree = xmalloc(count_nodes * sizeof *info.degree);
    info.color = xmalloc(count_nodes * sizeof *info.color);
    info.adj_list = xmalloc(count_nodes * sizeof *info.adj_list);

    for (var n = nodes; n; n = n->nl_list) {
        var node = n->nl_node;
        temp_t* temp_for_node = Table_get(interference->lvig_gtemp, node);
        assert(temp_for_node);

        if (Table_get(initial_allocation, temp_for_node)) {
            info.precolored = list_cons(node, info.precolored);
        } else {
            info.initial = list_cons(node, info.initial);
        }

        lv_node_list_t* adj = lv_adj(node);
        for (var s = adj; s; s = s->nl_list) {
            temp_t* temp_for_node = Table_get(interference->lvig_gtemp, node);
            // degree is the number of adjacent but not precolored nodes
            var is_precolored = !!Table_get(initial_allocation, temp_for_node);
            if (!is_precolored) {
                info.degree[node->lvn_idx] += 1;

                info.adj_list[node->lvn_idx] =
                    list_cons(s->nl_node, info.adj_list[node->lvn_idx]);
            }
        }
        // FIXME: free (via rust) adj;
    }
    if (0) {
        fprintf(stderr, "len(precolored) = %d\n", list_length(info.precolored));
        fprintf(stderr, "len(initial) = %d\n", list_length(info.initial));

        fprintf(stderr, "degree = [");
        for (int i = 0; i < count_nodes; i++) {
            fprintf(stderr, "%d,", info.degree[i]);
        }
        fprintf(stderr, "]\n");
    }

    /*
     * :: MakeWorklist ::
     */
    for (var n = info.initial; n; n = info.initial) {
        var node = n->nl_node;
        if (info.degree[node->lvn_idx] >= info.K) {
            // add to spill_worklist
            info.initial = n->nl_list;
            n->nl_list = info.spill_worklist;
            info.spill_worklist = n;
        } else {
            // add to simplify_worklist
            info.initial = n->nl_list;
            n->nl_list = info.simplify_worklist;
            info.simplify_worklist = n;
        }
    }

    /*
     * The loop before "AssignColors", from "Main"
     */
    while (info.simplify_worklist || info.spill_worklist) {
        if (info.simplify_worklist) {
            simplify(&info);
        } else {
            select_spill(&info);
        }
    }

    assign_colors(&info);

    // Next to do! convert our colouring and spills into
    // the correct result structure.

    struct ra_color_result result = {};

    // Return Spills
    for (var n = info.spilled_nodes; n; n = n->nl_list) {
        var node = n->nl_node;
        temp_t* temp_for_node = Table_get(interference->lvig_gtemp, node);
        assert(temp_for_node);
        result.racr_spills =
            temp_list_cons(*temp_for_node, result.racr_spills);
    }

    // Return Allocation
    result.racr_allocation = Table_new(0, cmptemp, hashtemp);
    for (var n = info.precolored; n; n = n->nl_list) {
        var node = n->nl_node;
        temp_t* temp_for_node = Table_get(interference->lvig_gtemp, node);
        assert(temp_for_node);
        Table_put(result.racr_allocation,
                temp_for_node,
                Table_get(initial_allocation, temp_for_node));
    }
    for (var n = info.colored_nodes; n; n = n->nl_list) {
        var node = n->nl_node;
        temp_t* temp_for_node = Table_get(interference->lvig_gtemp, node);
        assert(temp_for_node);

        var color_idx = info.color[node->lvn_idx];
        var register_name = registers[color_idx];

        Table_put(
                result.racr_allocation,
                temp_for_node,
                // cast away const
                (void*)register_name);
    }

    if (0) {
        fprintf(stderr, "degree = [");
        for (int i = 0; i < count_nodes; i++) {
            fprintf(stderr, "%d,", info.degree[i]);
        }
        fprintf(stderr, "]\n");
    }

    // :: clean up ::

    // This does not clean up the actual nodes themselves...
    // Those came from Rust, so we need to write something that gives them
    // back.
    node_list_free(&info.spilled_nodes);
    node_list_free(&info.colored_nodes);
    node_list_free(&info.precolored);
    assert(info.initial == NULL);
    assert(info.simplify_worklist == NULL);
    assert(info.spill_worklist == NULL);

    // dealloc all adj_lists
    for (int i = 0; i < count_nodes; i++) {
        node_list_free(&(info.adj_list[i]));
    }
    free(info.adj_list); info.adj_list = NULL;

    free(info.degree); info.degree = NULL;
    free(info.color); info.color = NULL;

    return result;
}


struct instr_list_and_allocation
ra_alloc(
        temp_state_t* temp_state,
        assm_instr_t* body_instrs,
        ac_frame_t* frame,
        bool print_interference_and_return)
{
    // here: liveness analysis
    var flow_and_nodes = instrs2graph(body_instrs);
    lv_flowgraph_t* flow = flow_and_nodes.flowgraph;

    var igraph_and_table =
        intererence_graph(flow);
    if (print_interference_and_return) {
        igraph_show(stdout, igraph_and_table.igraph);
        struct instr_list_and_allocation result = {};
        return result;
    }


    extern const char* registers[]; // FIXME
    var color_result =
        ra_color(igraph_and_table.igraph, flow, frame->acf_temp_map, registers);

    // :: check for spilled nodes, and rewrite program if so ::
    if (color_result.racr_spills) {
        assert(!"TODO: spilling");
    }

    struct instr_list_and_allocation result = {
        .ra_instrs = body_instrs,
        .ra_allocation = color_result.racr_allocation
    };

    lv_free_graph(flow->lvfg_control); flow->lvfg_control = NULL;
    // todo: free nodes from flow_and_nodes
    Table_free(&igraph_and_table.live_outs);

    return result;
}
