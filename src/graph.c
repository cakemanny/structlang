#include "liveness.h"
#include "mem.h"
#include <assert.h>
#include <stdbool.h>
#include "array.h"

#define var __auto_type
#define NELEMS(A) ((sizeof A) / sizeof A[0])

// This is a bit hacky because we abuse that node_t is int
#define NODE_ARR_IT(it, arr) \
    int _i = 0, it; _i < (arr).len && (it = (arr).data[_i], true); _i++

typedef int node_t; // private representation of a node

// See https://nullprogram.com/blog/2023/10/05/ for ideas about dynamic arrays.
typedef struct {
    node_t* data;
    int len;
    int cap;
} node_array_t;

typedef struct {
    node_array_t succ;
    node_array_t pred;
} node_rep_t;

typedef struct {
    node_rep_t* data;
    int len;
    int cap;
} node_rep_array_t;

struct lv_graph_t {
     node_rep_array_t nodes;
};

lv_graph_t*
lv_new_graph()
{
    lv_graph_t* result = xmalloc(sizeof *result);
    return result;
}

size_t
lv_graph_length(const lv_graph_t* g)
{
    return g->nodes.len;
}

void
lv_free_graph(lv_graph_t* g)
{
    for (int i = 0; i < g->nodes.len; i++) {
        var x = g->nodes.data[i];
        arrfree(x.succ);
        arrfree(x.pred);
    }
    arrfree(g->nodes);
    free(g);
}

static lv_node_t*
lv_node_new(lv_graph_t* graph, size_t idx)
{
    lv_node_t* node = xmalloc(sizeof *node);
    node->lvn_graph = graph;
    node->lvn_idx = idx;
    return node;
}

lv_node_t*
lv_new_node(lv_graph_t* graph)
{
    node_rep_t node_rep = {};
    arrpush(&graph->nodes, node_rep);
    return lv_node_new(graph, graph->nodes.len - 1);
}

void
lv_free_node(lv_node_t* node)
{
    free(node);
}

static bool
node_array_contains(const node_array_t* haystack, node_t needle)
{
    for (int i = 0; i < haystack->len; i++) {
        if (haystack->data[i] == needle) {
            return true;
        }
    }
    return false;
}

void
lv_mk_edge(lv_node_t* from, lv_node_t* to)
{
    assert(from->lvn_graph == to->lvn_graph);

    var nodes = from->lvn_graph->nodes;
    if (!node_array_contains(&nodes.data[from->lvn_idx].succ, to->lvn_idx)) {
        arrpush(&nodes.data[from->lvn_idx].succ, to->lvn_idx);
        arrpush(&nodes.data[to->lvn_idx].pred, from->lvn_idx);
    }
}

// TODO: lv_rm_edge
// TODO: lv_print_graph


static lv_node_list_t*
node_list_cons(lv_node_t* node, lv_node_list_t* list)
{
    lv_node_list_t* cell = xmalloc(sizeof *cell);
    cell->nl_node = node;
    cell->nl_list = list;
    return cell;
}

lv_node_list_t*
lv_nodes(lv_graph_t* graph)
{
    lv_node_list_t* result = NULL;

    for (int i = graph->nodes.len - 1; i >= 0; i--) {
        lv_node_t* new_node = lv_node_new(graph, i);
        result = node_list_cons(new_node, result);
    }
    return result;
}

lv_node_it
lv_succ(lv_node_t* n)
{
    var graph = n->lvn_graph;
    node_array_t* a = &graph->nodes.data[n->lvn_idx].succ;
    return (lv_node_it){
        .node_array = a,
        .graph = graph,
        .i = 0,
    };
}

bool
lv_node_it_next(lv_node_it* it)
{
    var a = (node_array_t*)it->node_array;
    var has_next = it->i < a->len;
    if (has_next) {
        it->lvni_node.lvn_graph = it->graph;
        it->lvni_node.lvn_idx = a->data[it->i];
        it->i++;
    }
    return has_next;
}

void
lv_node_list_free(lv_node_list_t* nl)
{
    while (nl != NULL) {
        var to_free = nl;
        nl = nl->nl_list;
        free(to_free->nl_node); to_free->nl_node = NULL;
        free(to_free);
    }
}

// TODO: lv_pred

static int
node_qsort_cmp(const void* x, const void* y)
{
    const node_t* xx = x;
    const node_t* yy = y;
    return *xx - *yy;
}

static void
node_array_push_if_missing(node_array_t* a, node_t idx)
{
    if (!node_array_contains(a, idx)) {
        arrpush(a, idx);
    }
}

typedef struct {
    const node_array_t* a;
    int i;
    node_t idx;  // the node ID
} node_array_it_t;


static node_array_it_t
node_array_it(const node_array_t* a)
{
    return (node_array_it_t){ .a = a, .i = 0 };
}

static bool
node_array_it_next(node_array_it_t* it)
{
    var has_next = it->i < it->a->len;
    if (has_next) {
        it->idx = it->a->data[it->i];
        it->i++;
    }
    return has_next;
}


/*
 * Returns all nodes with an edge from or to `n`
 */
lv_node_list_t*
lv_adj(lv_node_t* node)
{
    var graph = node->lvn_graph;

    node_array_t adj = {};
    var node_rep = graph->nodes.data[node->lvn_idx];
    // TODO: implement some sort of Set
    for (var it = node_array_it(&node_rep.pred); node_array_it_next(&it);) {
        node_array_push_if_missing(&adj, it.idx);
    }
    for (NODE_ARR_IT(idx, node_rep.succ)) {
        node_array_push_if_missing(&adj, idx);
    }
    qsort(adj.data, adj.len, sizeof(adj.data[0]), node_qsort_cmp);

    lv_node_list_t* result = NULL;
    for (int i = adj.len - 1; i >= 0; i--) {
        var idx = adj.data[i];
        lv_node_t* new_node = lv_node_new(graph, idx);
        result = node_list_cons(new_node, result);
    }
    arrfree(adj);
    return result;
}

static bool
lv_is_succ(const lv_node_t* n, const lv_node_t* m)
{
    var node_rep = n->lvn_graph->nodes.data[n->lvn_idx];
    for (NODE_ARR_IT(idx, node_rep.succ)) {
        if (idx == m->lvn_idx) {
            return true;
        }
    }
    return false;

}

bool
lv_is_adj(const lv_node_t* n, const lv_node_t* m)
{
    assert(n->lvn_graph == m->lvn_graph);
    return lv_is_succ(m, n) || lv_is_succ(n, m);
}


bool
lv_eq(const lv_node_t* a, const lv_node_t* b)
{
    return a->lvn_graph == b->lvn_graph && a->lvn_idx == b->lvn_idx;
}

#include "test_harness.h"

void
test_graph()
{

    var g = lv_new_graph();
    assert(lv_graph_length(g) == 0);

    var n = lv_new_node(g);
    assert(lv_graph_length(g) == 1);

    var m = lv_new_node(g);
    assert(lv_graph_length(g) == 2);

    assert(!lv_eq(n, m));
    assert(!lv_is_adj(n, m));

    lv_mk_edge(n, m);
    assert(lv_is_adj(n, m));
    assert(lv_is_adj(m, n));

    assert(lv_is_succ(n, m));
    assert(!lv_is_succ(m, n));

    lv_free_node(n);
    lv_free_node(m);
    lv_free_graph(g);
}


static void register_tests() __attribute__((constructor));
void
register_tests() {

    REGISTER_TEST(test_graph);

}
