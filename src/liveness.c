#include <stddef.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include "liveness.h"
#include "mem.h"
#include "list.h"

#define var __auto_type

static const bool debug = 0;

static temp_list_t* temp_list_sort(temp_list_t* tl);

// a free function that can be mapped over tables
static void vfree(const void* key, void** value, void* cl)
{
    free(*value);
    // our table doesn't allow null values
    // *value = NULL;
}
static void freeif(void*const ptr)
{
    if (ptr)
        free(ptr);
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

struct flowgraph_and_node_list instrs2graph(const assm_instr_t* instrs)
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

    // TODO: actually return this?
    lv_node_list_t* nodes = NULL;

    const assm_instr_t* prev = NULL;
    for (var instr = instrs; instr; instr = instr->ai_list) {
        var node = lv_new_node(graph);
        nodes = list_cons(node, nodes);

        switch (instr->ai_tag) {
            case ASSM_INSTR_OPER:
                if (nodes->nl_list) {
                    // make an edge from the previous node to this one
                    lv_mk_edge(nodes->nl_list->nl_node, node);
                }
                if (instr->ai_oper_dst) {
                    Table_put(def, node, temp_list_sort(instr->ai_oper_dst));
                }
                if (instr->ai_oper_src) {
                    Table_put(use, node, temp_list_sort(instr->ai_oper_src));
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

                Table_put(def, node, temp_list(instr->ai_move_dst));
                Table_put(use, node, temp_list(instr->ai_move_src));
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
                if (!target_node) {
                    fprintf(stderr, "no node for label: %s\n", lbl);
                } else {
                    // FIXME: always run this assert (once we've fixed the bug)
                    assert(target_node);
                    lv_mk_edge(node, target_node);
                }
            }
        }
    }
    Table_free(&label_to_node);

    struct flowgraph_and_node_list result = {
        flow_graph, nodes
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
static temp_list_t* temp_list_sort(temp_list_t* tl)
{
    int len = list_length(tl);
    if (len < 2) {
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
        result = temp_list_cons(temp_array[i], result);
    }
    return result;
}

#define temp_cmp(a, b) ((a).temp_id - (b).temp_id)

static void check_sorted(const temp_list_t* tl)
{
    for (var t = tl; t && t->tmp_list; t = t->tmp_list) {
        assert(temp_cmp(t->tmp_temp, t->tmp_list->tmp_temp) < 0);
    }
}

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


/*
 * Add all elements in b into a
 * Any added elements are copied in to newly allocated memory, so a owns all
 * memory in its list.
 */
/*XXX*/ void temp_list_union_l(temp_list_t** pa, const temp_list_t* b)
{
    if (debug) {
        check_sorted(*pa);
        check_sorted(b);
    }

#define a (*pa)
#define ADVANCE_A() pa = &a->tmp_list;
#define ADVANCE_B() b = b->tmp_list;
    while (a && b) {
        var cmp = temp_cmp(a->tmp_temp, b->tmp_temp);
        if (cmp < 0) {
            ADVANCE_A();
        } else if (cmp == 0) {
            ADVANCE_A();
            ADVANCE_B();
        } else {
            var cell = temp_list(b->tmp_temp);
            // insert at the current point of a
            cell->tmp_list = a;
            *pa = cell;
            ADVANCE_A();
            ADVANCE_B();
        }
    }
    while (b) {
        // pa points to the final tmp_list
        // allocate and insert remaining elements
        var cell = temp_list(b->tmp_temp);
        // insert at the current point of a
        cell->tmp_list = a;
        *pa = cell;
        ADVANCE_A();
        ADVANCE_B();
    }
#undef a
#undef ADVANCE_A
#undef ADVANCE_B
}
#define temp_list_union_r(a, pb) temp_list_union_l(pb, a)

/*XXX*/ temp_list_t* temp_list_minus(const temp_list_t* a, const temp_list_t* b)
{
    if (debug) {
        check_sorted(a);
        check_sorted(b);
    }

    temp_list_t* result = NULL;

    for (; a; a = a->tmp_list) {

        // advance b so that it is greater or equal to a-head
        while(b && temp_cmp(b->tmp_temp, a->tmp_temp) < 0) {
            b = b->tmp_list;
        }

        if (b && temp_cmp(a->tmp_temp, b->tmp_temp) == 0) {
            // skip both
        } else {
            result = temp_list_cons(a->tmp_temp, result);
        }
    }
    return list_reverse(result);
}

/*XXX*/ bool temp_list_eq(const temp_list_t* a, const temp_list_t* b)
{
    while (a || b) {
        if (!(a && b)) {
            return false;
        }
        if (temp_cmp(a->tmp_temp, b->tmp_temp) != 0) {
            return false;
        }
        a = a->tmp_list;
        b = b->tmp_list;
    }
    return true;
}

// prove that we can use the list.h routines on it
static_assert(sizeof(lv_node_pair_t) == sizeof(struct list_t),
        "lv_node_pair_t size");
static_assert(sizeof(lv_node_pair_list_t) == sizeof(struct list_t),
        "lv_node_pair_list_t size");

lv_node_pair_t* lv_node_pair(lv_node_t* m, lv_node_t* n)
{
    assert(m);
    assert(n);
    return list_cons(m, n);
}

lv_node_pair_list_t* lv_node_pair_cons(
        lv_node_pair_t* hd, lv_node_pair_list_t* tl)
{
    assert(hd);
    return list_cons(hd, tl);
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

#define BitsetLen(len) (((len) + 63) / 64)
#define IsBitSet(x, i) (( (x)[(i)>>6] & (1ULL<<((i)&63)) ) != 0ULL)
#define SetBit(x, i) (x)[(i)>>6] |= (1ULL<<((i)&63))
#define ClearBit(x, i) (x)[(i)>>6] &= (1ULL<<((i)&63)) ^ 0xFFFFFFFFFFFFFFFFULL

typedef struct node_set {
    size_t len;
    uint64_t bits[0]; /* variable length */
} node_set_t;

#define T node_set_t*
static T
node_set_new(size_t max_size)
{
    T result = xmalloc(
            sizeof *result + BitsetLen(max_size) * sizeof result->bits[0]);
    result->len = max_size;
    return result;
}

#define nbytes(len) BitsetLen(len) * sizeof(uint64_t)

#define setop(sequal, snull, tnull, op) \
	if (s == t) { assert(s); return sequal; } \
	else if (s == NULL) { assert(t); return snull; } \
	else if (t == NULL) return tnull; \
	else { \
		assert(s->len == t->len); \
		T set = node_set_new(s->len); \
		for (int i = BitsetLen(s->len); --i >= 0; ) { \
			set->bits[i] = s->bits[i] op t->bits[i]; \
        } \
		return set; }

static T copy(const T t) {
	assert(t);
	T set = node_set_new(t->len);
	if (t->len > 0) {
        memcpy(set->bits, t->bits, nbytes(t->len));
    }
	return set;
}

static T node_set_union(const T s, const T t) {
	setop(copy(t), copy(t), copy(s), |)
}

static T node_set_minus(const T s, const T t)
{
    setop(node_set_new(s->len),
            node_set_new(s->len), copy(s), & ~);
}
static int node_set_count(const T s)
{
    int total = 0;
    for (int i = BitsetLen(s->len); --i >= 0; ) {
        total += __builtin_popcountll(s->bits[i]);
    }
    return total;
}
static bool node_set_eq(const T s, const T t)
{
	assert(s && t);
	assert(s->len == t->len);
	for (int i = BitsetLen(s->len); --i >= 0; ) {
        if (s->bits[i] != t->bits[i])
            return false;
    }
	return true;
}
static void node_set_add(T s, const lv_node_t* node)
{
    SetBit(s->bits, node->lvn_idx);
}
/*XXX*/ void node_set_member(T s, const lv_node_t* node)
{
    IsBitSet(s->bits, node->lvn_idx);
}

static int node_set_first_idx(T s)
{
    for (int i = 0; i < BitsetLen(s->len); i++) {
        if (s->bits[i] != 0) {
            return __builtin_ctzll(s->bits[i]) + (64 * i);
        }
    }
    assert(!"set it empty");
}

#undef T

/* we define a wrapper struct for table: lv_node_t* -> node_set_t* */
#define K const lv_node_t*
#define V node_set_t*
#define T Table_NL
typedef struct Table_node_liveset { Table_T t; } T;

T Table_NL_new()
{
    T t = {Table_new(0, cmpnode, hashnode)};
    return t;
}
void Table_NL_free(T* table)
{
    Table_map(table->t, vfree, NULL);
    Table_free(&table->t);
}
V Table_NL_put(T table, K key, V value)
{
    return Table_put(table.t, key, value);
}
V Table_NL_get(T table, K key)
{
    return Table_get(table.t, key);
}
V Table_NL_remove(T table, K key)
{
    return Table_remove(table.t, key);
}

#undef T
#undef V
#undef K

static lv_node_t* ig_get_node_by_idx(lv_igraph_t* igraph, int i)
{
    lv_node_t fake_node = {.lvn_graph=igraph->lvig_graph, .lvn_idx=i};
    var ptemp = Table_get(igraph->lvig_gtemp, &fake_node);
    return ig_get_node_for_temp(igraph, ptemp);
}

struct igraph_and_table intererence_graph(
        lv_flowgraph_t* flow, lv_node_list_t* nodes)
{
    // First ensure that there is an interference graph node for
    // each temporary
    lv_igraph_t* igraph = xmalloc(sizeof *igraph);
    igraph->lvig_graph = lv_new_graph();
    igraph->lvig_tnode = Table_new(0, cmptemp, hashtemp);
    igraph->lvig_gtemp = Table_new(0, cmpnode, hashnode);
    igraph->lvig_moves = NULL;

    for (var n = nodes; n; n = n->nl_list) {
        temp_list_t* def_n = Table_get(flow->lvfg_def, n->nl_node);
        for (var d = def_n; d; d = d->tmp_list) {
            ig_get_node_for_temp(igraph, &d->tmp_temp);
        }
        temp_list_t* use_n = Table_get(flow->lvfg_use, n->nl_node);
        for (var u = use_n; u; u = u->tmp_list) {
            ig_get_node_for_temp(igraph, &u->tmp_temp);
        }
    }

    size_t graph_length = lv_graph_length(igraph->lvig_graph);

    // TODO: order nodes by depth-first search
    // this will work ok for a bit
    nodes = list_reverse(nodes);

    // compute live out
    // live out
    Table_NL live_in_map = Table_NL_new();
    Table_NL live_out_map = Table_NL_new();
    Table_NL def_map = Table_NL_new();
    Table_NL use_map = Table_NL_new();

    for (var n = nodes; n; n = n->nl_list) {
        Table_NL_put(live_in_map, n->nl_node, node_set_new(graph_length));
        Table_NL_put(live_out_map, n->nl_node, node_set_new(graph_length));

        var def_ns = node_set_new(graph_length);
        Table_NL_put(def_map, n->nl_node, def_ns);
        var use_ns = node_set_new(graph_length);
        Table_NL_put(use_map, n->nl_node, use_ns);

        temp_list_t* def_n = Table_get(flow->lvfg_def, n->nl_node);
        for (var d = def_n; d; d = d->tmp_list) {
            var d_node = ig_get_node_for_temp(igraph, &d->tmp_temp);
            node_set_add(def_ns, d_node);
        }
        temp_list_t* use_n = Table_get(flow->lvfg_use, n->nl_node);
        for (var u = use_n; u; u = u->tmp_list) {
            var u_node = ig_get_node_for_temp(igraph, &u->tmp_temp);
            node_set_add(use_ns, u_node);
        }
    }


    Table_NL live_in_map_ = Table_NL_new();
    Table_NL live_out_map_ = Table_NL_new();

    // calculate live-in and live-out sets iteratively
    for (;;) {
        for (var n = nodes; n; n = n->nl_list) {
            var node = n->nl_node;
            // copy the previous iteration
            // TODO: collect and free the old sets / reuse them a/b style
            // in'[n] = in[n]; out'[n] = out[n]
            freeif(Table_NL_put(live_in_map_, node, Table_NL_get(live_in_map, node)));
            node_set_t* out_ns = Table_NL_get(live_out_map, node);
            freeif(Table_NL_put(live_out_map_, node, out_ns));

            // in[n] = use[n] union (out[n] setminus def[n])
            node_set_t* use_n = Table_NL_get(use_map, node);
            node_set_t* def_n = Table_NL_get(def_map, node);

            var out_minus_def = node_set_minus(out_ns, def_n);
            node_set_t* in_n = node_set_union(use_n, out_minus_def);

            // out[n] = union {in[s] for s in succ[n]}
            node_set_t* out_n = node_set_new(graph_length);
            lv_node_list_t* succ = lv_succ(node);
            for (var s = succ; s; s = s->nl_list) {
                node_set_t* in_s = Table_NL_get(live_in_map, s->nl_node);
                assert(in_s);
                var prev_out_n = out_n;
                out_n = node_set_union(prev_out_n, in_s);
                free(prev_out_n);
            }
            lv_node_list_free(succ);

            // store results back into live_in_map and live_out_map
            Table_NL_put(live_out_map, node, out_n);

            Table_NL_put(live_in_map, node, in_n);
        }

        bool match = true;
        for (var n = nodes; n; n = n->nl_list) {
            var node = n->nl_node;

            node_set_t* in_ns_ = Table_NL_get(live_in_map_, node);
            node_set_t* in_ns = Table_NL_get(live_in_map, node);
            if (!node_set_eq(in_ns_, in_ns)) {
                match = false;
                break;
            }

            node_set_t* out_ns_ = Table_NL_get(live_out_map_, node);
            node_set_t* out_ns = Table_NL_get(live_out_map, node);
            if (!node_set_eq(out_ns_, out_ns)) {
                match = false;
                break;
            }
        }
        if (match)
            break;
    }

    // we are done with live_in_map and the diff maps
    Table_NL_free(&live_in_map_);
    Table_NL_free(&live_out_map_);
    Table_NL_free(&live_in_map);

    // Now we have the live-out sets, we can compute the interference graph

    // ok, now we need a node in the graph for each temp?
    // and a way of associating them

    // 1. At any non-move instruction the defs from that instruction
    // interfere with the live-outs at that instruction
    // 2. At any move instruction a <- c , b in live-outs interferes with a
    // if b != c.
    for (var n = nodes; n; n = n->nl_list) {
        var node = n->nl_node;
        node_set_t* def_n = Table_NL_get(def_map, node);
        node_set_t* use_n = Table_NL_get(use_map, node);
        node_set_t* out_ns = Table_NL_get(live_out_map, node);

        for (int i = 0; i < def_n->len; i++) {
            if (IsBitSet(def_n->bits, i)) {
                lv_node_t* d_node = ig_get_node_by_idx(igraph, i);

                if (!Table_get(flow->lvfg_ismove, node)) {
                    for (int j = 0; j < out_ns->len; j++) {
                        if (IsBitSet(out_ns->bits, j)) {
                            lv_node_t* t_node = ig_get_node_by_idx(igraph, j);
                            lv_mk_edge(d_node, t_node);
                        }
                    }
                } else {
                    assert(node_set_count(use_n) == 1);
                    var u_idx = node_set_first_idx(use_n);

                    lv_node_t* u_node = ig_get_node_by_idx(igraph, u_idx);
                    igraph->lvig_moves = lv_node_pair_cons(
                            lv_node_pair(d_node, u_node), igraph->lvig_moves);

                    for (int j = 0; j < out_ns->len; j++) {
                        if (IsBitSet(out_ns->bits, j)) {
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

    Table_T live_outs = Table_new(0, cmpnode, hashnode);

    // convert the live outs into a map just to temp_list
    for (var n = nodes; n; n = n->nl_list) {
        node_set_t* out_ns = Table_NL_remove(live_out_map, n->nl_node);
        if (node_set_count(out_ns) > 0) {
            temp_list_t* out_temps = NULL;
            for (int j = 0; j < out_ns->len; j++) {
                if (IsBitSet(out_ns->bits, j)) {
                    lv_node_t fake_node = {.lvn_graph=igraph->lvig_graph, .lvn_idx=j};
                    temp_t* ptemp = Table_get(igraph->lvig_gtemp, &fake_node);
                    out_temps = temp_list_cons(*ptemp, out_temps);
                }
            }
            Table_put(live_outs, n->nl_node, out_temps);
        }
    }
    Table_NL_free(&live_out_map);

    // Put the nodes back in order before we are caught!
    nodes = list_reverse(nodes);

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
    // TODO: work out how to track which values in the live_outs to free
    Table_free(&igraph_and_live_outs->live_outs);
    Table_free(&igraph_and_live_outs->igraph->lvig_tnode);
    Table_free(&igraph_and_live_outs->igraph->lvig_gtemp);
    // TODO: lvig_moves;

    // Free flow graph
    lv_free_graph(flow_and_nodes->flowgraph->lvfg_control);
    flow_and_nodes->flowgraph->lvfg_control = NULL;

    Table_free(&flow_and_nodes->flowgraph->lvfg_def);
    Table_free(&flow_and_nodes->flowgraph->lvfg_use);
    Table_free(&flow_and_nodes->flowgraph->lvfg_ismove);

    // free nodes from flow_and_nodes
    for (lv_node_list_t *n = flow_and_nodes->node_list, *next=NULL; n; n = next) {
        // read the field in advance of freeing the memory
        next = n->nl_list;

        lv_free_node(n->nl_node); n->nl_node = NULL;
        n->nl_list = NULL;
        free(n);
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
sorted_temps_for_nodes(lv_igraph_t* igraph, lv_node_list_t* nodes)
{
    temp_list_t* temps = NULL;
    for (var s = nodes; s; s = s->nl_list) {
        temp_t* temp_for_node = Table_get(igraph->lvig_gtemp, s->nl_node);
        assert(temp_for_node);
        temps = temp_list_cons(*temp_for_node, temps);
    }
    temp_list_t* sorted_temps = temp_list_sort(temps);
    if (sorted_temps != temps) {
        temp_list_free(&temps);
    }
    return sorted_temps;
}

void igraph_show(FILE* out, lv_igraph_t* igraph)
{

    fprintf(out, "# ---- Interference Graph ----\n");

    var nodes = lv_nodes(igraph->lvig_graph);
    var sorted_temps = sorted_temps_for_nodes(igraph, nodes);

    for (var t = sorted_temps; t; t = t->tmp_list) {

        fprintf(out, "# %d [", t->tmp_temp.temp_id);

        var node = Table_get(igraph->lvig_tnode, &t->tmp_temp);
        lv_node_list_t* adj = lv_adj(node);

        var sorted_temps = sorted_temps_for_nodes(igraph, adj);
        for (var t = sorted_temps; t; t = t->tmp_list) {
            fprintf(out, "%d,", t->tmp_temp.temp_id);
        }
        temp_list_free(&sorted_temps);
        lv_node_list_free(adj);
        fprintf(out, "]\n");
    }
    fprintf(out, "# ----------------------------\n");
    temp_list_free(&sorted_temps);
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
