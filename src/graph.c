#include "liveness.h"
#include <assert.h>
#include <stdbool.h>
#include "array.h"

#define var __auto_type
#define NELEMS(A) ((sizeof A) / sizeof A[0])
#define Alloc(ar, size) Arena_alloc(ar, size, __FILE__, __LINE__)

// This is a bit hacky because we abuse that node_t is int
#define NODE_ARR_IT(it, arr) \
    int _i = 0, it; _i < (arr).len && (it = (arr).data[_i], true); _i++

typedef int node_t; // private representation of a node

// See https://nullprogram.com/blog/2023/10/05/ for ideas about dynamic arrays.
typedef arrtype(node_t) node_array_t;

typedef struct {
    node_array_t succ;
    node_array_t pred;
} node_rep_t;

typedef arrtype(node_rep_t) node_rep_array_t;

struct lv_graph_t {
     node_rep_array_t nodes;
     Arena_T arena; // for allocations of internal structures
};

lv_graph_t*
lv_new_graph(Arena_T ar)
{
    lv_graph_t* result = Alloc(ar, sizeof *result);
    result->arena = ar;
    return result;
}

size_t
lv_graph_length(const lv_graph_t* g)
{
    return g->nodes.len;
}

static lv_node_t*
lv_node_new(lv_graph_t* graph, size_t idx, Arena_T ar)
{
    lv_node_t* node = Alloc(ar, sizeof *node);
    node->lvn_graph = graph;
    node->lvn_idx = idx;
    return node;
}

lv_node_t*
lv_new_node(lv_graph_t* graph, Arena_T ar)
{
    node_rep_t node_rep = {};
    arrpush(&graph->nodes, graph->arena, node_rep);
    return lv_node_new(graph, graph->nodes.len - 1, ar);
}

static int
int_binary_search(int* a, int from, int to, int key)
{
    int low = from;
    int high = to;

    while (low <= high) {
        int mid = (low + high) >> 1;
        int midVal = a[mid];

        if (midVal < key)
            low = mid + 1;
        else if (midVal > key)
            high = mid - 1;
        else
            return mid;  // key found
    }
    return -(low + 1);
}

static bool
node_array_contains(const node_array_t* haystack, node_t needle)
{
    return int_binary_search(haystack->data, 0, haystack->len - 1, needle) >= 0;
}

static void
node_array_keep_sorted(node_array_t* a)
{
    int i = a->len - 1;
    var data = a->data;
    for (; (i >= 1) && (data[i - 1] > data[i]); --i) {
        var tmp = data[i - 1];
        data[i - 1] = data[i];
        data[i] = tmp;
    }
}

void
lv_mk_edge(lv_node_t* from, lv_node_t* to)
{
    assert(from->lvn_graph == to->lvn_graph);

    var arena = from->lvn_graph->arena;
    var nodes = from->lvn_graph->nodes;
    if (!node_array_contains(&nodes.data[from->lvn_idx].succ, to->lvn_idx)) {
        arrpush(&nodes.data[from->lvn_idx].succ, arena, to->lvn_idx);
        node_array_keep_sorted(&nodes.data[from->lvn_idx].succ);
        arrpush(&nodes.data[to->lvn_idx].pred, arena, from->lvn_idx);
        node_array_keep_sorted(&nodes.data[from->lvn_idx].pred);
    }
}

// TODO: lv_rm_edge
// TODO: lv_print_graph


lv_node_it_virt
lv_nodes(lv_graph_t* graph)
{
    lv_node_it_virt result = {
        .lvni_node = {
            .lvn_graph = graph,
            .lvn_idx = 0,
        },
        .i = 0,
    };
    return result;
}

bool
_lv_node_it_virt_next(lv_node_it_virt* it)
{
    var graph = it->lvni_node.lvn_graph;
    var has_next = it->i < lv_graph_length(graph);
    if (has_next) {
        it->lvni_node.lvn_idx = it->i;
        it->i++;
    }
    return has_next;
}

lv_node_it_arr
lv_succ(lv_node_t* n)
{
    var graph = n->lvn_graph;
    node_array_t* a = &graph->nodes.data[n->lvn_idx].succ;
    return (lv_node_it_arr){
        .node_array = a,
        .graph = graph,
        .i = 0,
    };
}

lv_node_it_arr
lv_pred(lv_node_t* n)
{
    var graph = n->lvn_graph;
    node_array_t* a = &graph->nodes.data[n->lvn_idx].pred;
    return (lv_node_it_arr){
        .node_array = a,
        .graph = graph,
        .i = 0,
    };
}

bool
_lv_node_it_arr_next(lv_node_it_arr* it)
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

bool
_lv_node_it_2arr_next(lv_node_it_2arr* it)
{
    const node_array_t* a[2];
    a[0] = it->node_array[0];
    a[1] = it->node_array[1];
#define has_next(i_) (it->i[(i_)] < a[(i_)]->len)
#define next(i_) (a[(i_)]->data[it->i[(i_)]])
    int j = it->j;
    var has_next = has_next(j);
    if (has_next) {
        it->lvni_node.lvn_idx = next(j);

        // advance
        it->i[j]++;

        if (has_next(0) && has_next(1)) {
            if (next(0) == next(1)) {
                it->i[1]++;
                it->j = 0;
            } else {
                it->j = (next(0) < next(1)) ? 0 : 1;
            }
        } else {
            it->j = (has_next(0)) ? 0 : 1;
        }
    }
    return has_next;
#undef next
#undef has_next
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


/*
 * Returns an iterator of nodes with an edge from or to `n`
 */
lv_node_it_2arr
lv_adj(lv_node_t* node)
{
    var graph = node->lvn_graph;

    lv_node_it_2arr it = {};
    it.lvni_node.lvn_graph = graph;
    it.node_array[0] = &graph->nodes.data[node->lvn_idx].pred;
    it.node_array[1] = &graph->nodes.data[node->lvn_idx].succ;
    it.i[0] = it.i[1] = 0;

    var node_rep = graph->nodes.data[node->lvn_idx];
    if (node_rep.pred.len > 0 && node_rep.succ.len > 0) {
        // j points the the array with the min next value;
        it.j = (node_rep.pred.data[0] > node_rep.succ.data[0]);
    } else if (node_rep.succ.len > 0) {
        it.j = 1;
    }

    return it;
}

static bool
lv_is_succ(const lv_node_t* n, const lv_node_t* m)
{
    var n_succ = n->lvn_graph->nodes.data[n->lvn_idx].succ;
    return node_array_contains(&n_succ, m->lvn_idx);
}

bool
lv_is_adj(const lv_node_t* n, const lv_node_t* m)
{
    assert(n->lvn_graph == m->lvn_graph);
    return lv_is_succ(m, n) || lv_is_succ(n, m);
}


#include "test_harness.h"

void
test_graph()
{

    var a = Arena_new();
    var g = lv_new_graph(a);
    assert(lv_graph_length(g) == 0);

    var n = lv_new_node(g, a);
    assert(lv_graph_length(g) == 1);

    var m = lv_new_node(g, a);
    assert(lv_graph_length(g) == 2);

    assert(!lv_eq(n, m));
    assert(!lv_is_adj(n, m));

    lv_mk_edge(n, m);
    assert(lv_is_adj(n, m));
    assert(lv_is_adj(m, n));

    assert(lv_is_succ(n, m));
    assert(!lv_is_succ(m, n));

    Arena_dispose(&a);
}


static void register_tests() __attribute__((constructor));
void
register_tests() {

    REGISTER_TEST(test_graph);

}
