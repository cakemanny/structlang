#include "liveness.h"
#include "list.h"
#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define var __auto_type
#define Alloc(ar, size) Arena_alloc(ar, size, __FILE__, __LINE__)

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

static const bool debug = 0;

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
node_set_table_new(size_t count, size_t max_elems, Arena_T ar)
{
    assert(count >= 1);
    assert(max_elems >= 1);

    node_set_table_t table = {
        .count = count,
        .len = max_elems,
    };
    table.bits = Alloc(ar, count * BitsetBytes(max_elems));
    return table;
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

static void node_set2_remove(node_set2_t s, const lv_node_t* node)
{
    ClearBit(s.bits, node->lvn_idx);
}

static bool node_set2_member(node_set2_t s, const lv_node_t* node)
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
    abort();
}

// https://nullprogram.com/blog/2018/07/31/
uint32_t lv_node_hash(lv_node_t* n)
{
    uint32_t x = n->lvn_idx;
    x ^= x >> 15;
    x *= 0x2c1b3c6dU;
    x ^= x >> 12;
    x *= 0x297a2d39U;
    x ^= x >> 15;
    return x;
}
temp_list_t* *nt_upsert(lv_node_temps_map_t **m, lv_node_t* key, Arena_T ar)
{
    for (uint32_t h = lv_node_hash(key); *m; h <<= 2) {
        if (lv_eq(key, (*m)->key)) {
            return &(*m)->value;
        }
        m = &(*m)->child[h>>30];
    }
    if (!ar) {
        return NULL;
    }
    *m = Alloc(ar, sizeof **m);
    (*m)->key = key;
    return &(*m)->value;
}
temp_list_t* nt_get(lv_node_temps_map_t *m, lv_node_t* key)
{
    temp_list_t** ptl = nt_upsert(&m, key, NULL);
    if (ptl == NULL) {
        return NULL;
    }
    return *ptl;
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
    lv_flowgraph_t* flow_graph = Alloc(arena, sizeof *flow_graph);
    var graph = flow_graph->lvfg_control = lv_new_graph(arena);

    typeof(flow_graph->lvfg_def) def = NULL;
    typeof(flow_graph->lvfg_use) use = NULL;
    flow_graph->lvfg_ismove = NULL;

    /*
     * A lookup to the start node of each basic block, using the label
     */
    Table_T label_to_node = Table_new(0, NULL, NULL);

    lv_node_list_t* nodes = NULL;

    const assm_instr_t* prev = NULL;
    for (var instr = instrs; instr; instr = instr->ai_list) {
        var node = lv_new_node(graph, arena);
        nodes = list_cons(node, nodes, arena);

        switch (instr->ai_tag) {
            case ASSM_INSTR_OPER:
                if (nodes->nl_list) {
                    // make an edge from the previous node to this one
                    lv_mk_edge(nodes->nl_list->nl_node, node);
                }
                if (instr->ai_oper_dst) {
                    *nt_upsert(&def, node, arena) =
                            temp_list_sort(instr->ai_oper_dst, arena);
                }
                if (instr->ai_oper_src) {
                    *nt_upsert(&use, node, arena) =
                            temp_list_sort(instr->ai_oper_src, arena);
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

                *nt_upsert(&def, node, arena) = temp_list(instr->ai_move_dst, arena);
                *nt_upsert(&use, node, arena) = temp_list(instr->ai_move_src, arena);
                nodeset_upsert(&flow_graph->lvfg_ismove, node, arena);
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

    flow_graph->lvfg_def = def;
    flow_graph->lvfg_use = use;
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
        // It's only valid to not copy this if the src list outlives the
        // destination. In this case, instructions outlive liveness.
        return tl;
    }
    long nbytes = len * sizeof(temp_t);
    // there are multiple calls to this per instruction - so for those cases we
    // use this stack array to avoid lots of small leaks. Expected number of
    // calls with a large N is small.
    enum { fixed_max = 1024 };
    temp_t fixed_size[fixed_max];
    temp_t* temp_array = (len > fixed_max) ? Alloc(ar, nbytes) : fixed_size;
    int i = 0;
    for (var t = tl; t; t = t->tmp_list, i++) {
        temp_array[i] = t->tmp_temp;
    }

    qsort(temp_array, len, sizeof(struct temp), cmptemp);

    temp_list_t* result = NULL;
    for (int i = len - 1; i >= 0; --i) {
        result = temp_list_cons(temp_array[i], result, ar);
    }
    return result;
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


static lv_node_t*
ig_get_node_for_temp(lv_igraph_t* igraph, temp_t* ptemp, Arena_T ar)
{
    lv_node_t* ig_node = Table_get(igraph->lvig_tnode, ptemp);
    if (!ig_node) {
        ig_node = lv_new_node(igraph->lvig_graph, ar);
        Table_put(igraph->lvig_tnode, ptemp, ig_node);
        Table_put(igraph->lvig_gtemp, ig_node, ptemp);
    }
    return ig_node;
}


static lv_node_t*
ig_get_node_by_idx(lv_igraph_t* igraph, int i, Arena_T ar)
{
    lv_node_t fake_node = {.lvn_graph=igraph->lvig_graph, .lvn_idx=i};
    var ptemp = Table_get(igraph->lvig_gtemp, &fake_node);
    return ig_get_node_for_temp(igraph, ptemp, ar);
}


static void
depth_first_search(int* N, lv_node_t* i, bool* mark, lv_node_t* sorted)
{
    // TODO: change this into a work-stack based algorithm
    // instead of recursive function
    if (mark[i->lvn_idx] == false) {
        mark[i->lvn_idx] = true;
        for (var it = lv_pred(i); lv_node_it_next(&it);) {
            depth_first_search(N, &it.lvni_node, mark, sorted);
        }
        sorted[*N - 1] = *i;
        *N = *N - 1;
    }
}

static lv_node_t*
topological_sort(int flowgraph_length, lv_node_list_t* cg_nodes, Arena_T ar)
{
    int N = flowgraph_length;
    bool* mark = Alloc(ar, N * sizeof *mark);
    lv_node_t* sorted = Alloc(ar, N * sizeof *sorted);

    lv_node_t* exit_node = NULL;
    for (var n = cg_nodes; n; n = n->nl_list) {
        int k = 0;
        for (var it = lv_succ(n->nl_node); lv_node_it_next(&it);) {
            k++; }
        if (k == 0) {
            exit_node = n->nl_node;
            break;
        }
    }
    assert(exit_node);

    depth_first_search(&N, exit_node, mark, sorted);
    return sorted;
}

/*
 * Given a control flow graph (and its associated nodes), compute
 * the interference graph and the Live Outs for each flow graph node.
 */
struct igraph_and_table
interference_graph(
        lv_flowgraph_t* flow, lv_node_list_t* cg_nodes, Arena_T arena)
{
    Arena_T scratch = Arena_new();
    // First ensure that there is an interference graph node for
    // each temporary
    lv_igraph_t* igraph = Alloc(arena, sizeof *igraph);
    igraph->lvig_graph = lv_new_graph(arena);
    igraph->lvig_tnode = Table_new(0, cmptemp, hashtemp);
    igraph->lvig_gtemp = Table_new(0, cmpnode, hashnode);
    igraph->lvig_moves = NULL;

    /*
     * Add nodes to the interference graph for each temporary
     */
    for (var n = cg_nodes; n; n = n->nl_list) {
        temp_list_t* def_n = nt_get(flow->lvfg_def, n->nl_node);
        for (var d = def_n; d; d = d->tmp_list) {
            ig_get_node_for_temp(igraph, &d->tmp_temp, arena);
        }
        temp_list_t* use_n = nt_get(flow->lvfg_use, n->nl_node);
        for (var u = use_n; u; u = u->tmp_list) {
            ig_get_node_for_temp(igraph, &u->tmp_temp, arena);
        }
    }

    // compute live outs
    size_t igraph_len = lv_graph_length(igraph->lvig_graph);
    size_t fg_len = lv_graph_length(flow->lvfg_control);

    // using a topological sort means we navigate the control flow graph
    // from the end backwards. The speeds up the settling of iterative
    // equations.
    // FIXME: this should use scratch, but we are leaking the references into
    // our graph.
    lv_node_t* sorted = topological_sort(fg_len, cg_nodes, arena);

    node_set_table_t live_in_map = node_set_table_new(fg_len, igraph_len, scratch);
    node_set_table_t live_out_map = node_set_table_new(fg_len, igraph_len, scratch);
    node_set_table_t def_map = node_set_table_new(fg_len, igraph_len, scratch);
    node_set_table_t use_map = node_set_table_new(fg_len, igraph_len, scratch);

    for (int i = 0; i < fg_len; i++) {
        var node = &sorted[i];
        var def_ns = Table_NST_get(def_map, node);
        var use_ns = Table_NST_get(use_map, node);

        temp_list_t* def_n = nt_get(flow->lvfg_def, node);
        for (var d = def_n; d; d = d->tmp_list) {
            var d_node = ig_get_node_for_temp(igraph, &d->tmp_temp, arena);
            node_set2_add(def_ns, d_node);
        }
        temp_list_t* use_n = nt_get(flow->lvfg_use, node);
        for (var u = use_n; u; u = u->tmp_list) {
            var u_node = ig_get_node_for_temp(igraph, &u->tmp_temp, arena);
            node_set2_add(use_ns, u_node);
        }
    }


    node_set_table_t live_in_map_ = node_set_table_new(fg_len, igraph_len, scratch);
    node_set_table_t live_out_map_ = node_set_table_new(fg_len, igraph_len, scratch);

    // space allocated for sets that we use within our loop.
    node_set_table_t loop_statics = node_set_table_new(3, igraph_len, scratch);
    node_set2_t out_n = node_set_table_get(loop_statics, 0);
    node_set2_t in_n = node_set_table_get(loop_statics, 1);
    node_set2_t out_minus_def = node_set_table_get(loop_statics, 2);

    node_set_table_t fgraph_sets = node_set_table_new(3, fg_len, scratch);
    // Algo 17.6 from the book adapted for liveness - i.e. we run it backwards
    node_set2_t worklist = node_set_table_get(fgraph_sets, 0);

    for (int i = 0; i < fg_len; i++) {
        var node = &sorted[i];
        node_set2_add(worklist, node);
    }

    // calculate live-in and live-out sets iteratively
    int iterations = 0;
    for (; node_set2_count(worklist) > 0; iterations++) {
        // todo: Investigate if priority queue / treap might be better for
        // retrieval.
        lv_node_t* node = NULL;
        for (int i = 0; i < fg_len; i++) {
            node = &sorted[i];
            if (node_set2_member(worklist, node)) {
                break;
            }
        }
        node_set2_remove(worklist, node);

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

        // check for changes
        node_set2_t in_ns_ = Table_NST_get(live_in_map_, node);
        node_set2_t in_ns = Table_NST_get(live_in_map, node);
        node_set2_t out_ns_ = Table_NST_get(live_out_map_, node);

        if (!node_set2_eq(in_ns_, in_ns) || !node_set2_eq(out_ns_, out_ns)) {

            node_set2_add(worklist, node);
            for (var it = lv_pred(node); lv_node_it_next(&it);) {
                node_set2_add(worklist, &it.lvni_node);
            }
        }
    }
    if (debug) {
        fprintf(stderr, "## iterations = %d, fglen = %lu\n", iterations,
                fg_len);
    }

    // Now we have the live-out sets, we can compute the interference graph

    // ok, now we need a node in the graph for each temp?
    // and a way of associating them

    // 1. At any non-move instruction the defs from that instruction
    // interfere with the live-outs at that instruction
    // 2. At any move instruction a <- c , b in live-outs interferes with a
    // if b != c.
    for (int i = 0; i < fg_len; i++) {
        var node = &sorted[i];
        node_set2_t def_n = Table_NST_get(def_map, node);
        node_set2_t use_n = Table_NST_get(use_map, node);
        node_set2_t out_ns = Table_NST_get(live_out_map, node);

        for (int i = 0; i < def_n.len; i++) {
            if (IsBitSet(def_n.bits, i)) {
                lv_node_t* d_node = ig_get_node_by_idx(igraph, i, arena);

                if (!nodeset_ismember(flow->lvfg_ismove, node)) {
                    for (int j = 0; j < out_ns.len; j++) {
                        if (IsBitSet(out_ns.bits, j)) {
                            lv_node_t* t_node =
                                ig_get_node_by_idx(igraph, j, arena);
                            lv_mk_edge(d_node, t_node);
                        }
                    }
                } else {
                    assert(node_set2_count(use_n) == 1);
                    var u_idx = node_set2_first_idx(use_n);

                    lv_node_t* u_node = ig_get_node_by_idx(igraph, u_idx, arena);
                    igraph->lvig_moves = lv_node_pair_cons(
                            lv_node_pair(d_node, u_node, arena),
                            igraph->lvig_moves, arena);

                    for (int j = 0; j < out_ns.len; j++) {
                        if (IsBitSet(out_ns.bits, j)) {
                            lv_node_t* t_node =
                                ig_get_node_by_idx(igraph, j, arena);
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

    lv_node_temps_map_t* live_outs = NULL;

    // convert the live outs into a map just to temp_list
    for (int i = 0; i < fg_len; i++) {
        var node = &sorted[i];
        node_set2_t out_ns = Table_NST_get(live_out_map, node);
        if (node_set2_count(out_ns) > 0) {
            temp_list_t* out_temps = NULL;
            for (int j = 0; j < out_ns.len; j++) {
                if (IsBitSet(out_ns.bits, j)) {
                    lv_node_t fake_node = {.lvn_graph=igraph->lvig_graph, .lvn_idx=j};
                    temp_t* ptemp = Table_get(igraph->lvig_gtemp, &fake_node);
                    out_temps = temp_list_cons(*ptemp, out_temps, arena);
                }
            }
            *nt_upsert(&live_outs, node, arena) = out_temps;
        }
    }

    Arena_dispose(&scratch);
    struct igraph_and_table result = {
        .igraph = igraph,
        .live_outs = live_outs,
    };
    return result;
}

void
lv_free_interference_and_flow_graph(
        struct igraph_and_table* igraph_and_live_outs,
        struct flowgraph_and_node_list* flow_and_nodes)
{
    // Free interference graph
    Table_free(&igraph_and_live_outs->igraph->lvig_tnode);
    Table_free(&igraph_and_live_outs->igraph->lvig_gtemp);
}

// Yeah... so... the return value must be used immediately, or copied by the
// caller
char* lv_nodename(lv_node_t* node)
{
    static char buf[64] = {};
    snprintf(buf, 64, "%lu", node->lvn_idx);
    return buf;
}

void
flowgraph_print(
        FILE* out, const lv_flowgraph_t* flow_graph,
        const lv_node_list_t* node_list, const assm_instr_t* instrs,
        const target_t* target)
{
    fprintf(out, "# ---- Control Flow Graph ----\n");
    var nd = node_list;
    for (var instr = instrs; instr; instr = instr->ai_list, nd = nd->nl_list) {
        var node = nd->nl_node;

        fprintf(out, "# %3lu [", node->lvn_idx);
        const char* fmt = "%2lu";
        for (var it = lv_succ(node); lv_node_it_next(&it);) {
            fprintf(out, fmt, it.lvni_node.lvn_idx);
            fmt = ", %2lu";
        }
        fprintf(out, "] ");
        char buf[128];
        assm_format(buf, 128, instr,
                target->tgt_temp_map(), target);
        fprintf(out, "%s", buf);
    }
    fprintf(out, "# ----------------------------\n");
}

static temp_list_t*
temps_for_graph_nodes(lv_igraph_t* igraph, Arena_T ar)
{
    temp_list_t* temps = NULL;
    for (var it = lv_nodes(igraph->lvig_graph); lv_node_it_next(&it); ) {
        temp_t* temp_for_node = Table_get(igraph->lvig_gtemp, &it.lvni_node);
        assert(temp_for_node);
        temps = temp_list_cons(*temp_for_node, temps, ar);
    }
    return temps;
}

static temp_list_t*
temps_for_adj(lv_igraph_t* igraph, lv_node_t* node, Arena_T ar)
{
    temp_list_t* temps = NULL;
    for (var it = lv_nodes(igraph->lvig_graph); lv_node_it_next(&it); ) {
        if (lv_is_adj(node, &it.lvni_node)) {
            temp_t* temp_for_node = Table_get(igraph->lvig_gtemp, &it.lvni_node);
            assert(temp_for_node);
            temps = temp_list_cons(*temp_for_node, temps, ar);
        }
    }
    return temps;
}

void igraph_show(FILE* out, lv_igraph_t* igraph)
{
    fprintf(out, "# ---- Interference Graph ----\n");

    var scratch = Arena_new();
    var sorted_temps = temp_list_sort(
            temps_for_graph_nodes(igraph, scratch), scratch);

    for (var t = sorted_temps; t; t = t->tmp_list) {

        fprintf(out, "# %d [", t->tmp_temp.temp_id);

        var node = Table_get(igraph->lvig_tnode, &t->tmp_temp);

        var sorted_adj_temps = temp_list_sort(
            temps_for_adj(igraph, node, scratch), scratch);
        for (var t = sorted_adj_temps; t; t = t->tmp_list) {
            fprintf(out, "%d,", t->tmp_temp.temp_id);
        }
        fprintf(out, "]\n");
    }
    fprintf(out, "# ----------------------------\n");
    Arena_dispose(&scratch);

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
