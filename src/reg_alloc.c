#include "reg_alloc.h"
#include "liveness.h"
#include <assert.h>

#define var __auto_type


/*
 * Holds all of our worklists and state, etc for the graph colouring
 * algorithm.
 */
typedef struct reg_alloc_info_t {

    const int K; // The number of registers on the machine

    lv_node_list_t* simplify_worklist;
    lv_node_list_t* spill_worklist;

    lv_node_list_t* select_stack;

    int* degree; // an array containing the degree of each node.

} reg_alloc_info_t;

// if we haven't implemented coalescing then
// def get_alias(n): return n


/*
 * node_list_remove removes the list cell from the worklist, for the
 * node. it then returns it to the caller.
 */
lv_node_list_t*
node_list_remove(lv_node_list_t** pworklist, lv_node_t* node)
{
    var p = pworklist;
    for (; *p; p = &((*p)->nl_list)) {
        if ((*p)->nl_node->lvn_idx == node->lvn_idx) {

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

/*
 *
 */
static void decrement_degree(
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
// TODO: make static once in use
void simplify(
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


    var node = info->simplify_worklist->nl_node;

    lv_node_list_t* adj = lv_adj(node);
    for (var s = adj; s; s = s->nl_list) {
        var m = s->nl_node;
        decrement_degree(info, m);
        // TODO: free node list cell and node?
    }
}


struct ra_color_result {
    Table_T racr_allocation; // temp_t -> register (char*)
    temp_list_t* racr_temp_list; // a list of spills
} ra_color(
    lv_igraph_t* interference,
    Table_T initial_allocation,
    int (*spill_cost)(lv_node_t*),
    /* registers is just a list of all machine registers */
    temp_list_t* registers) // maybe this could be an array?
{
    /*
     * // :: make worklist ::
     * // if not spilling, then everything goes into simplifyWorklist
     *
     * for n in initial:
     *   if degree[n] >= len(registers):
     *      spillWorklist.add(n)
     *   else:
     *      simplifyWorklist.add(n)
     *
     * while (simplifyWorklist and spillWorklist) {
     *   if (simplifyWorklist) {
     *     simplify()
     *   } else if (spillWorklist) {
     *     selectSpill()
     *   }
     * }
     *
     * // :: assign colors ::
     * while(selectStack) {
     *   n = pop(selectStack);
     *   okColors = [0 .. len(registers) - 1];
     *   adjList_n = lv_adj(n);
     *   for (w in adjList_n) {
     *     if (get_alias(w) in coloredNodes or get_alias(w) in precolored) {
     *       remove color[get_alias(w)] from okColors
     *     }
     *   }
     *   if (len(okColors) == 0) {
     *     assert(!"TODO: spilling")
     *     spilledNodes add n
     *   } else {
     *     coloredNodes add n
     *   }
     * }
     * if coalescing:
     *   for n in coalescedNode:
     *     color[n] = color[get_alias(n)]
     *
     * // :: check for spilled nodes, and rewrite program if so ::
     */
    struct ra_color_result result = {};
    return result;
}


struct instr_list_and_allocation
ra_alloc(
        temp_state_t* temp_state,
        assm_instr_t* body_instrs,
        ac_frame_t* frame)
{
    // here: liveness analysis
    var flow_and_nodes = instrs2graph(body_instrs);
    lv_flowgraph_t* flow = flow_and_nodes.flowgraph;

    var igraph_and_table =
        intererence_graph(flow);
    igraph_show(stdout, igraph_and_table.igraph);

    lv_free_graph(flow->lvfg_control); flow->lvfg_control = NULL;
    // todo: free nodes from flow_and_nodes
    Table_free(&igraph_and_table.live_outs);

    // here: 

    // TODO: spilling and calling color
    struct instr_list_and_allocation result = {};
    return result;
}
