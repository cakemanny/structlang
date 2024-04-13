#include "liveness.h"
#include "mem.h"
#include "list.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define var __auto_type

static temp_list_t* temp_list_sort(temp_list_t* tl, Arena_T);

/*
 * In order to have a fast, allocation free liveness analysis,
 * we implement use and def sets as an array of bit sets.
 * That is we have a contiguous array of 64-bit words.
 *
 * Suppose there are 72 temporaries used within an example function.
 * This will require two 64-bit words to represent each set.
 * FIXME: we should talk about the number of control flow graph nodes.
 * So we would allocate 72 * 2 (= 144) 64-bit words to store a map
 * of sets for the interference graph.
 */

#define BitsetLen(len) (((len) + 63) / 64)
#define IsBitSet(x, i) (( (x)[(i)>>6] & (1ULL<<((i)&63)) ) != 0ULL)
#define SetBit(x, i) (x)[(i)>>6] |= (1ULL<<((i)&63))
#define ClearBit(x, i) (x)[(i)>>6] &= (1ULL<<((i)&63)) ^ 0xFFFFFFFFFFFFFFFFULL
#define BitsetBytes(len) (sizeof(uint64_t) * BitsetLen(len))

/*
 * This is a single set from our table. It points somewhere into the table.
 * It's passed by value to avoid double indirection.
 */
typedef struct node_set2 {
    size_t len;
    uint64_t* bits;
} node_set2_t;

typedef struct node_set_table {
    size_t count;  /* The number of sets in our table */
    size_t len;  /* The number of nodes in the graph.
                    The max number of elements in our set */
    uint64_t* bits;
} node_set_table_t;

static node_set_table_t
node_set_table_new(size_t count, size_t max_elems)
{
    assert(count >= 1);
    assert(max_elems >= 1);

    node_set_table_t table = {
        .count = count,
        .len = max_elems,
    };
    table.bits = xmalloc(count * BitsetBytes(max_elems));
    return table;
}
static void node_set_table_free(node_set_table_t* table)
{
    assert(table);
    assert(table->bits);
    free(table->bits);
    table->bits = NULL;
}
static node_set2_t node_set_table_get(node_set_table_t table, int idx)
{
    node_set2_t result = {
        .len = table.len,
        .bits = table.bits + BitsetLen(table.len) * idx,
    };
    return result;
}

static node_set2_t Table_NST_get(node_set_table_t table, lv_node_t* node)
{
    return node_set_table_get(table, node->lvn_idx);
}

/* overwrites the set for node with the value of to_copy */
static void Table_NST_put(node_set_table_t table, lv_node_t* node, node_set2_t to_copy)
{
    assert(table.len == to_copy.len);
    node_set2_t dst = Table_NST_get(table, node);
    memcpy(dst.bits, to_copy.bits, BitsetBytes(table.len));
}

static void node_set2_clear(node_set2_t dst)
{
    memset(dst.bits, 0, BitsetBytes(dst.len));
}

static void
node_set2_union(node_set2_t dst, const node_set2_t src1, const node_set2_t src2)
{
    for (int i = BitsetLen(dst.len); --i >= 0; ) {
        dst.bits[i] = src1.bits[i] | src2.bits[i];
    }
}

static void
node_set2_minus(node_set2_t dst, const node_set2_t src1, const node_set2_t src2)
{
    for (int i = BitsetLen(dst.len); --i >= 0; ) {
        dst.bits[i] = src1.bits[i] & ~src2.bits[i];
    }
}

static int node_set2_count(const node_set2_t s)
{
    int total = 0;
    for (int i = BitsetLen(s.len); --i >= 0; ) {
        total += __builtin_popcountll(s.bits[i]);
    }
    return total;
}

static bool node_set2_eq(const node_set2_t s, const node_set2_t t)
{
	assert(s.len == t.len);
	for (int i = BitsetLen(s.len); --i >= 0; ) {
        if (s.bits[i] != t.bits[i])
            return false;
    }
	return true;
}

static void node_set2_add(node_set2_t s, const lv_node_t* node)
{
    SetBit(s.bits, node->lvn_idx);
}

/*static*/ bool node_set2_member(node_set2_t s, const lv_node_t* node)
{
    return IsBitSet(s.bits, node->lvn_idx);
}

static int node_set2_first_idx(node_set2_t s)
{
    for (int i = 0, n = BitsetLen(s.len); i < n; i++) {
        if (s.bits[i] != 0) {
            return __builtin_ctzll(s.bits[i]) + (64 * i);
        }
    }
    assert(!"set is empty");
}



static int cmpnode(const void* x, const void* y)
{
    if (lv_eq(x, y)) {
        return 0;
    }
    return 1;
}
static unsigned hashnode(const void* key)
{
    const lv_node_t* k = key;
    return k->lvn_idx;
}

struct flowgraph_and_node_list
instrs2graph(const assm_instr_t* instrs, Arena_T arena)
{
    lv_flowgraph_t* flow_graph = xmalloc(sizeof *flow_graph);
    var graph = flow_graph->lvfg_control = lv_new_graph();

    var def = flow_graph->lvfg_def = Table_new(0, cmpnode, hashnode);
    var use = flow_graph->lvfg_use = Table_new(0, cmpnode, hashnode);
    var ismove = flow_graph->lvfg_ismove = Table_new(0, cmpnode, hashnode);

    /*
     * A lookup to the start node of each basic block, using the label
     */
    Table_T label_to_node = Table_new(0, NULL, NULL);

    lv_node_list_t* nodes = NULL;

    const assm_instr_t* prev = NULL;
    for (var instr = instrs; instr; instr = instr->ai_list) {
        var node = lv_new_node(graph);
        nodes = list_cons(node, nodes, arena);

        switch (instr->ai_tag) {
            case ASSM_INSTR_OPER:
                if (nodes->nl_list) {
                    // make an edge from the previous node to this one
                    lv_mk_edge(nodes->nl_list->nl_node, node);
                }
                if (instr->ai_oper_dst) {
                    Table_put(def, node,
                            temp_list_sort(instr->ai_oper_dst, arena));
                }
                if (instr->ai_oper_src) {
                    Table_put(use, node,
                            temp_list_sort(instr->ai_oper_src, arena));
                }
                break;
            case ASSM_INSTR_LABEL:
                // we only definitely fall through to this if the previous
                // instruction doesn't have jump targets
                if (prev &&
                        !(prev->ai_tag == ASSM_INSTR_OPER
                            && prev->ai_oper_jump)) {
                    lv_mk_edge(nodes->nl_list->nl_node, node);
                }
                // since we also need to consider jumps to this node
                // we squirrel this node away in a map by label
                Table_put(label_to_node, instr->ai_label, node);
                break;
            case ASSM_INSTR_MOVE:
                if (nodes->nl_list) {
                    // make an edge from the previous node to this one
                    lv_mk_edge(nodes->nl_list->nl_node, node);
                }

                Table_put(def, node, temp_list(instr->ai_move_dst, arena));
                Table_put(use, node, temp_list(instr->ai_move_src, arena));
                Table_put(ismove, node, (void*)1);
                break;
        }
        prev = instr;
    }
    nodes = list_reverse(nodes);

    var nd = nodes;
    for (var instr = instrs; instr; instr = instr->ai_list, nd = nd->nl_list) {
        var node = nd->nl_node;
        if (instr->ai_tag == ASSM_INSTR_OPER && instr->ai_oper_jump) {
            for (int i = 0; instr->ai_oper_jump[i]; i++) {
                var lbl = instr->ai_oper_jump[i];
                var target_node = Table_get(label_to_node, lbl);
                assert(target_node);
                lv_mk_edge(node, target_node);
            }
        }
    }
    Table_free(&label_to_node);

    struct flowgraph_and_node_list result = {
        .flowgraph = flow_graph,
        .node_list = nodes,
    };
    return result;
}

/*
 * if x < y result is negative
 * if x == y result is 0
 * if x > y result is positive
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

// prove that we can use the list.h routines on it
static_assert(sizeof(temp_list_t) == sizeof(struct list_t), "temp_list_t size");

// sorts a temp list
// may or may not return the same list
// NB: Must not sort in place, since these lists are used in the formatting
// of the operations when they are omitted
static temp_list_t* temp_list_sort(temp_list_t* tl, Arena_T ar)
{
    int len = list_length(tl);
    if (len < 2) {
        // FIXME: don't we need to copy this?
        return tl;
    }
    temp_t temp_array[len];
    int i = 0;
    for (var t = tl; t; t = t->tmp_list, i++) {
        memcpy(temp_array + i, &t->tmp_temp, sizeof t->tmp_temp);
    }

    qsort(temp_array, len, sizeof(struct temp), cmptemp);

    temp_list_t* result = NULL;
    for (int i = len - 1; i >= 0; --i) {
        result = temp_list_cons(temp_array[i], result, ar);
    }
    return result;
}

#define temp_cmp(a, b) ((a).temp_id - (b).temp_id)

void print_temp_list(const temp_list_t* a)
{
    fprintf(stderr, "[");
    for (; a; a = a->tmp_list)
    {
        if (a->tmp_list) {
            fprintf(stderr, "%d, ", a->tmp_temp.temp_id);
        } else {
            fprintf(stderr, "%d", a->tmp_temp.temp_id);
        }
    }
    fprintf(stderr, "]\n");
}


// prove that we can use the list.h routines on it
static_assert(sizeof(lv_node_pair_t) == sizeof(struct list_t),
        "lv_node_pair_t size");
static_assert(sizeof(lv_node_pair_list_t) == sizeof(struct list_t),
        "lv_node_pair_list_t size");

lv_node_pair_t* lv_node_pair(lv_node_t* m, lv_node_t* n, Arena_T ar)
{
    assert(m);
    assert(n);
    return list_cons(m, n, ar);
}

lv_node_pair_list_t* lv_node_pair_cons(
        lv_node_pair_t* hd, lv_node_pair_list_t* tl, Arena_T ar)
{
    assert(hd);
    return list_cons(hd, tl, ar);
}

bool lv_node_pair_eq(const lv_node_pair_t* lhs, const lv_node_pair_t* rhs)
{
    return lv_eq(lhs->np_node0, rhs->np_node0) &&
        lv_eq(lhs->np_node1, rhs->np_node1);
}


static lv_node_t* ig_get_node_for_temp(lv_igraph_t* igraph, temp_t* ptemp)
{
    lv_node_t* ig_node = Table_get(igraph->lvig_tnode, ptemp);
    if (!ig_node) {
        ig_node = lv_new_node(igraph->lvig_graph);
        Table_put(igraph->lvig_tnode, ptemp, ig_node);
        Table_put(igraph->lvig_gtemp, ig_node, ptemp);
    }
    return ig_node;
}


static lv_node_t* ig_get_node_by_idx(lv_igraph_t* igraph, int i)
{
    lv_node_t fake_node = {.lvn_graph=igraph->lvig_graph, .lvn_idx=i};
    var ptemp = Table_get(igraph->lvig_gtemp, &fake_node);
    return ig_get_node_for_temp(igraph, ptemp);
}

/*
 * Given a control flow graph (and its associated nodes), compute
 * the interference graph and the Live Outs for each flow graph node.
 */
struct igraph_and_table
interference_graph(
        lv_flowgraph_t* flow, lv_node_list_t* cg_nodes, Arena_T arena)
{
    // First ensure that there is an interference graph node for
    // each temporary
    lv_igraph_t* igraph = xmalloc(sizeof *igraph);
    igraph->lvig_graph = lv_new_graph();
    igraph->lvig_tnode = Table_new(0, cmptemp, hashtemp);
    igraph->lvig_gtemp = Table_new(0, cmpnode, hashnode);
    igraph->lvig_moves = NULL;

    /*
     * Add nodes to the interference graph for each temporary
     */
    for (var n = cg_nodes; n; n = n->nl_list) {
        temp_list_t* def_n = Table_get(flow->lvfg_def, n->nl_node);
        for (var d = def_n; d; d = d->tmp_list) {
            ig_get_node_for_temp(igraph, &d->tmp_temp);
        }
        temp_list_t* use_n = Table_get(flow->lvfg_use, n->nl_node);
        for (var u = use_n; u; u = u->tmp_list) {
            ig_get_node_for_temp(igraph, &u->tmp_temp);
        }
    }


    // TODO: order nodes by depth-first search
    // A simple initial strategy is to rever order the control graph nodes.
    // By my testing, this halfs the number of iterations required.
    cg_nodes = list_reverse(cg_nodes);

    // compute live out
    // live out
    size_t igraph_length = lv_graph_length(igraph->lvig_graph);
    size_t flowgraph_length = lv_graph_length(flow->lvfg_control);

    node_set_table_t live_in_map = node_set_table_new(flowgraph_length, igraph_length);
    node_set_table_t live_out_map = node_set_table_new(flowgraph_length, igraph_length);
    node_set_table_t def_map = node_set_table_new(flowgraph_length, igraph_length);
    node_set_table_t use_map = node_set_table_new(flowgraph_length, igraph_length);

    for (var n = cg_nodes; n; n = n->nl_list) {
        var def_ns = Table_NST_get(def_map, n->nl_node);
        var use_ns = Table_NST_get(use_map, n->nl_node);

        temp_list_t* def_n = Table_get(flow->lvfg_def, n->nl_node);
        for (var d = def_n; d; d = d->tmp_list) {
            var d_node = ig_get_node_for_temp(igraph, &d->tmp_temp);
            node_set2_add(def_ns, d_node);
        }
        temp_list_t* use_n = Table_get(flow->lvfg_use, n->nl_node);
        for (var u = use_n; u; u = u->tmp_list) {
            var u_node = ig_get_node_for_temp(igraph, &u->tmp_temp);
            node_set2_add(use_ns, u_node);
        }
    }


    node_set_table_t live_in_map_ = node_set_table_new(flowgraph_length, igraph_length);
    node_set_table_t live_out_map_ = node_set_table_new(flowgraph_length, igraph_length);

    // space allocated for sets that we use within our loop.
    node_set_table_t loop_statics = node_set_table_new(3, igraph_length);
    node_set2_t out_n = node_set_table_get(loop_statics, 0);
    node_set2_t in_n = node_set_table_get(loop_statics, 1);
    node_set2_t out_minus_def = node_set_table_get(loop_statics, 2);

    // calculate live-in and live-out sets iteratively
    for (;;) {
        for (var n = cg_nodes; n; n = n->nl_list) {
            var node = n->nl_node;
            // copy the previous iteration
            // in'[n] = in[n]; out'[n] = out[n]
            Table_NST_put(live_in_map_, node, Table_NST_get(live_in_map, node));
            node_set2_t out_ns = Table_NST_get(live_out_map, node);
            Table_NST_put(live_out_map_, node, out_ns);

            // in[n] = use[n] union (out[n] setminus def[n])
            node_set2_t use_n = Table_NST_get(use_map, node);
            node_set2_t def_n = Table_NST_get(def_map, node);

            node_set2_minus(out_minus_def, out_ns, def_n);
            node_set2_union(in_n, use_n, out_minus_def);

            // out[n] = union {in[s] for s in succ[n]}
            node_set2_clear(out_n);
            for (var it = lv_succ(node); lv_node_it_next(&it);) {
                node_set2_t in_s = Table_NST_get(live_in_map, &it.lvni_node);
                node_set2_union(out_n, out_n, in_s);
            }

            // store results back into live_in_map and live_out_map
            Table_NST_put(live_out_map, node, out_n);
            Table_NST_put(live_in_map, node, in_n);
        }

        bool match = true;
        for (var n = cg_nodes; n; n = n->nl_list) {
            var node = n->nl_node;

            node_set2_t in_ns_ = Table_NST_get(live_in_map_, node);
            node_set2_t in_ns = Table_NST_get(live_in_map, node);
            if (!node_set2_eq(in_ns_, in_ns)) {
                match = false;
                break;
            }

            node_set2_t out_ns_ = Table_NST_get(live_out_map_, node);
            node_set2_t out_ns = Table_NST_get(live_out_map, node);
            if (!node_set2_eq(out_ns_, out_ns)) {
                match = false;
                break;
            }
        }
        if (match)
            break;
    }

    // we are done with live_in_map and the diff maps
    node_set_table_free(&loop_statics);
    node_set_table_free(&live_in_map_);
    node_set_table_free(&live_out_map_);
    node_set_table_free(&live_in_map);

    // Now we have the live-out sets, we can compute the interference graph

    // ok, now we need a node in the graph for each temp?
    // and a way of associating them

    // 1. At any non-move instruction the defs from that instruction
    // interfere with the live-outs at that instruction
    // 2. At any move instruction a <- c , b in live-outs interferes with a
    // if b != c.
    for (var n = cg_nodes; n; n = n->nl_list) {
        var node = n->nl_node;
        node_set2_t def_n = Table_NST_get(def_map, node);
        node_set2_t use_n = Table_NST_get(use_map, node);
        node_set2_t out_ns = Table_NST_get(live_out_map, node);

        for (int i = 0; i < def_n.len; i++) {
            if (IsBitSet(def_n.bits, i)) {
                lv_node_t* d_node = ig_get_node_by_idx(igraph, i);

                if (!Table_get(flow->lvfg_ismove, node)) {
                    for (int j = 0; j < out_ns.len; j++) {
                        if (IsBitSet(out_ns.bits, j)) {
                            lv_node_t* t_node = ig_get_node_by_idx(igraph, j);
                            lv_mk_edge(d_node, t_node);
                        }
                    }
                } else {
                    assert(node_set2_count(use_n) == 1);
                    var u_idx = node_set2_first_idx(use_n);

                    lv_node_t* u_node = ig_get_node_by_idx(igraph, u_idx);
                    igraph->lvig_moves = lv_node_pair_cons(
                            lv_node_pair(d_node, u_node, arena),
                            igraph->lvig_moves, arena);

                    for (int j = 0; j < out_ns.len; j++) {
                        if (IsBitSet(out_ns.bits, j)) {
                            lv_node_t* t_node = ig_get_node_by_idx(igraph, j);
                            // self moves don't interfere
                            if (lv_eq(t_node, u_node)) {
                                continue;
                            }
                            lv_mk_edge(d_node, t_node);
                        }
                    }
                }
            }
        }
    }
    // we are done with the def and use maps (until we convert our
    // control flow graph structure)
    node_set_table_free(&def_map);
    node_set_table_free(&use_map);

    Table_T live_outs = Table_new(0, cmpnode, hashnode);

    // convert the live outs into a map just to temp_list
    for (var n = cg_nodes; n; n = n->nl_list) {
        node_set2_t out_ns = Table_NST_get(live_out_map, n->nl_node);
        if (node_set2_count(out_ns) > 0) {
            temp_list_t* out_temps = NULL;
            for (int j = 0; j < out_ns.len; j++) {
                if (IsBitSet(out_ns.bits, j)) {
                    lv_node_t fake_node = {.lvn_graph=igraph->lvig_graph, .lvn_idx=j};
                    temp_t* ptemp = Table_get(igraph->lvig_gtemp, &fake_node);
                    out_temps = temp_list_cons(*ptemp, out_temps, arena);
                }
            }
            Table_put(live_outs, n->nl_node, out_temps);
        }
    }
    node_set_table_free(&live_out_map);

    // Put the nodes back in order before we are caught!
    cg_nodes = list_reverse(cg_nodes);

    struct igraph_and_table result = {
        .igraph = igraph,
        .live_outs = live_outs,
    };
    return result;
}

static void
tnode_entry_free(const void* key, void** value, void* cl)
{
    lv_node_t* ig_node = *value;
    lv_free_node(ig_node);
}

void
lv_free_interference_and_flow_graph(
        struct igraph_and_table* igraph_and_live_outs,
        struct flowgraph_and_node_list* flow_and_nodes)
{
    // Free interference graph
    // values referenced by live_outs are on an arena
    Table_free(&igraph_and_live_outs->live_outs);
    lv_free_graph(igraph_and_live_outs->igraph->lvig_graph);
    igraph_and_live_outs->igraph->lvig_graph = NULL;
    Table_map(igraph_and_live_outs->igraph->lvig_tnode, tnode_entry_free, NULL);
    Table_free(&igraph_and_live_outs->igraph->lvig_tnode);
    Table_free(&igraph_and_live_outs->igraph->lvig_gtemp);
    // lvig_moves: on an arena
    free(igraph_and_live_outs->igraph); igraph_and_live_outs->igraph = NULL;

    // Free flow graph
    lv_free_graph(flow_and_nodes->flowgraph->lvfg_control);
    flow_and_nodes->flowgraph->lvfg_control = NULL;

    Table_free(&flow_and_nodes->flowgraph->lvfg_def);
    Table_free(&flow_and_nodes->flowgraph->lvfg_use);
    Table_free(&flow_and_nodes->flowgraph->lvfg_ismove);
    free(flow_and_nodes->flowgraph); flow_and_nodes->flowgraph = NULL;

    // free nodes from flow_and_nodes
    for (lv_node_list_t *n = flow_and_nodes->node_list, *next=NULL; n; n = next) {
        // read the field in advance of freeing the memory
        next = n->nl_list;

        lv_free_node(n->nl_node); n->nl_node = NULL;
        n->nl_list = NULL;
        // we don't free n, as it was allocated on an arena
    }
    flow_and_nodes->node_list = NULL;

}

// Yeah... so... the return value must be used immediately, or copied by the
// caller
char* lv_nodename(lv_node_t* node)
{
    static char buf[64] = {};
    snprintf(buf, 64, "%lu", node->lvn_idx);
    return buf;
}

static temp_list_t*
sorted_temps_for_nodes(lv_igraph_t* igraph, lv_node_list_t* nodes, Arena_T ar)
{
    temp_list_t* temps = NULL;
    for (var s = nodes; s; s = s->nl_list) {
        temp_t* temp_for_node = Table_get(igraph->lvig_gtemp, s->nl_node);
        assert(temp_for_node);
        temps = temp_list_cons(*temp_for_node, temps, ar);
    }
    return temp_list_sort(temps, ar);
}

void igraph_show(FILE* out, lv_igraph_t* igraph)
{

    fprintf(out, "# ---- Interference Graph ----\n");

    var nodes = lv_nodes(igraph->lvig_graph);
    var scratch = Arena_new();
    var sorted_temps = sorted_temps_for_nodes(igraph, nodes, scratch);

    for (var t = sorted_temps; t; t = t->tmp_list) {

        fprintf(out, "# %d [", t->tmp_temp.temp_id);

        var node = Table_get(igraph->lvig_tnode, &t->tmp_temp);
        lv_node_list_t* adj = lv_adj(node);

        var sorted_temps = sorted_temps_for_nodes(igraph, adj, scratch);
        for (var t = sorted_temps; t; t = t->tmp_list) {
            fprintf(out, "%d,", t->tmp_temp.temp_id);
        }
        lv_node_list_free(adj);
        fprintf(out, "]\n");
    }
    fprintf(out, "# ----------------------------\n");
    Arena_dispose(&scratch);
    lv_node_list_free(nodes);

    fprintf(out, "# ----       Moves        ----\n");
    for (var mm = igraph->lvig_moves; mm; mm = mm->npl_list) {
        var m = mm->npl_node;
        {
            var dst_node = m->np_node0;
            temp_t* temp_for_node = Table_get(igraph->lvig_gtemp, dst_node);
            assert(temp_for_node);
            fprintf(out, "# %d <- ", temp_for_node->temp_id);
        }
        {
            var src_node = m->np_node1;
            temp_t* temp_for_node = Table_get(igraph->lvig_gtemp, src_node);
            assert(temp_for_node);
            fprintf(out, "%d\n", temp_for_node->temp_id);
        }
    }
    fprintf(out, "# ----------------------------\n");

}
