#include "reg_alloc.h"
#include "liveness.h"

#define var __auto_type

// if we haven't implemented coalescing then
// def get_alias(n): return n

struct ra_color_result {
    Table_T racr_allocation; // temp_t -> register (char*)
    temp_list_t* racr_temp_list;
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
     *   // Continue here
     *   // Read Simplify
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
ra_alloc(assm_instr_t* body_instrs, ac_frame_t* frame)
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
