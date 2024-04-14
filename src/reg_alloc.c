#include "reg_alloc.h"
#include "liveness.h"
#include <assert.h>
#include <string.h>
#include "list.h"
#include "codegen.h"

#define var __auto_type

#define BitsetLen(len) (((len) + 63) / 64)
#define IsBitSet(x, i) (( (x)[(i)>>6] & (1ULL<<((i)&63)) ) != 0ULL)
#define SetBit(x, i) (x)[(i)>>6] |= (1ULL<<((i)&63))
#define ClearBit(x, i) (x)[(i)>>6] &= (1ULL<<((i)&63)) ^ 0xFFFFFFFFFFFFFFFFULL

// Useful note for self when debugging:
// __builtin_debugtrap()
static const bool debug = 0;
static const bool enable_coalescing = true;

enum worklist_mem : unsigned char {
    WL_PRECOLORED = 0,
    WL_INITIAL,
    WL_SIMPLIFY,
    WL_FREEZE,
    WL_SPILL,
    WL_SPILLED,
    WL_COALESCED,
    WL_COLORED,
    WL_SELECT,
};

/*
 * Holds all of our worklists and state, etc for the graph colouring
 * algorithm.
 */
typedef struct reg_alloc_info_t {

    const int K; // The number of registers on the machine

    // Node worklists, sets, and stacks
    lv_node_list_t* precolored; // machine registers, preassigned a colour.
    lv_node_list_t* initial; // temps, not coloured and not yet processed
    lv_node_list_t* simplify_worklist; // low-degree non-move-related nodes
    lv_node_list_t* freeze_worklist; // low degree move-related nodes
    lv_node_list_t* spill_worklist; // high degree nodes
    lv_node_list_t* spilled_nodes;
    lv_node_list_t* coalesced_nodes; // registers that been coalesced. a set
    lv_node_list_t* colored_nodes;
    lv_node_list_t* select_stack;

    // Move sets
    lv_node_pair_list_t* coalesced_moves;
    lv_node_pair_list_t* constrained_moves;
    lv_node_pair_list_t* frozen_moves;
    lv_node_pair_list_t* worklist_moves;
    lv_node_pair_list_t* active_moves;

    int* degree; // an array containing the degree of each node.
    int* color; // colours assigned to the node with ID idx;
    lv_node_list_t** adj_list; // a list of non-precoloured adjacents
    // an array mapping each node to the list of moves it is associated with
    lv_node_pair_list_t** move_list;
    Table_T alias; // lv_node_t* -> lv_node_t*

    enum worklist_mem* worklist; // array of worklist membership for node ID

    lv_flowgraph_t* flowgraph;
    lv_igraph_t* interference;

    Arena_T scratch; // deallocated at end of ra_color

} reg_alloc_info_t;

static_assert(
        offsetof(reg_alloc_info_t, precolored) + (WL_SELECT * sizeof(void*)) ==
        offsetof(reg_alloc_info_t, select_stack),
        "offsetof(..., select_stack)");

static temp_t* temp_for_node(reg_alloc_info_t* info, lv_node_t* node)
{
    temp_t* result = Table_get(info->interference->lvig_gtemp, node);
    assert(result);
    return result;
}

static void print_adj_list(reg_alloc_info_t* info, lv_node_list_t* adj)
{
    fprintf(stderr, "adj_list = [");
    for (var w = adj; w; w = w->nl_list) {
        temp_t* t = temp_for_node(info, w->nl_node);
        fprintf(stderr, "%d,", t->temp_id);
    }
    fprintf(stderr, "]\n");
}

static void print_ok_colors(reg_alloc_info_t* info, uint64_t* ok_colors)
{
    fprintf(stderr, "ok_colors = [");
    for (int i = 0; i < info->K; i++) {
        if (IsBitSet(ok_colors, i)) {
            fprintf(stderr, "%d,", i);
        }
    }
    fprintf(stderr, "]\n");
}

/*static*/ void
print_node_list(reg_alloc_info_t* info, lv_node_list_t* node_list)
{
    fprintf(stderr, "[");
    for (var w = node_list; w; w = w->nl_list) {
        temp_t* t = temp_for_node(info, w->nl_node);
        fprintf(stderr, " %d.%d,", t->temp_id, t->temp_size);
    }
    fprintf(stderr, "]\n");
}

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

static void
node_list_prepend(lv_node_list_t** pworklist, lv_node_list_t* cell)
{
    cell->nl_list = *pworklist;
    *pworklist = cell;
}

static void
worklist_prepend(
        reg_alloc_info_t* info, enum worklist_mem wl, lv_node_list_t* cell)
{
    // This will make refactoring harder...
    lv_node_list_t** pnl =
        (lv_node_list_t**)(
                    (char*)info
                    + offsetof(reg_alloc_info_t, precolored)
                    + (wl * sizeof(void*)));
    node_list_prepend(pnl, cell);
    info->worklist[cell->nl_node->lvn_idx] = wl;
}

static bool
worklist_contains(
        reg_alloc_info_t* info, enum worklist_mem wl, const lv_node_t* node)
{
    return info->worklist[node->lvn_idx] == wl;
}


static bool
node_pair_list_contains(const lv_node_pair_list_t* haystack, const lv_node_pair_t* node_pair)
{
    for (var h = haystack; h; h = h->npl_list) {
        if (lv_node_pair_eq(h->npl_node, node_pair)) {
            return true;
        }
    }
    return false;
}

/*
 * similar to node_list_remove but for pair list
 */
static lv_node_pair_list_t*
node_pair_list_remove(lv_node_pair_list_t** pworklist, const lv_node_pair_t* pair)
{
    var p = pworklist;
    for (; *p; p = &((*p)->npl_list)) {
        if (lv_node_pair_eq((*p)->npl_node, pair)) {

            var found = *p;
            // unchain the cell we found
            *p = found->npl_list;
            // for sanity, ensure our cell is detached
            found->npl_list = NULL;
            return found;
        }
    }
    assert(!"not found");
}

/*
 * These two are used when we have Tables with temp_t's as keys
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
 * For Tables using nodes as keys
 */
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

static bool
temp_eq(temp_t a, temp_t b)
{
    return a.temp_id == b.temp_id;
}

static temp_t*
temp_copy_to_arena(Arena_T arena, temp_t src)
{
    temp_t* dst = Arena_alloc(arena, sizeof src, __FILE__, __LINE__);
    temp_copy(dst, &src);
    return dst;
}

static bool
temp_list_contains(const temp_list_t* haystack, temp_t temp)
{
    for (var h = haystack; h; h = h->tmp_list) {
        if (temp_eq(h->tmp_temp, temp)) {
            return true;
        }
    }
    return false;
}

static int
get_degree(reg_alloc_info_t* info, lv_node_t* n)
{
    return info->degree[n->lvn_idx];
}

static lv_node_t*
get_alias(reg_alloc_info_t* info, lv_node_t* n)
{
    if (worklist_contains(info, WL_COALESCED, n)) {
        var alias = Table_get(info->alias, n);
        assert(alias);
        return get_alias(info, alias);
    }
    return n;
}

static void
print_coalesced_nodes(reg_alloc_info_t* info)
{

    fprintf(stderr, "coalesced_nodes = [");
    for (var w = info->coalesced_nodes; w; w = w->nl_list) {
        temp_t* t = temp_for_node(info, w->nl_node);
        var alias_node = get_alias(info, w->nl_node);
        temp_t* alias = temp_for_node(info, alias_node);
        fprintf(stderr, " %d.%d -> %d.%d,",
                t->temp_id, t->temp_size,
                alias->temp_id, alias->temp_size);
    }
    fprintf(stderr, "]\n");
}

typedef struct node_move_it {
    reg_alloc_info_t* info;
    lv_node_pair_list_t* next;
} node_move_it_t;

static void
node_move_it_init(node_move_it_t* it, reg_alloc_info_t* info, lv_node_t* node)
{
    it->info = info;
    it->next = info->move_list[node->lvn_idx];
}

static lv_node_pair_t*
node_move_it_next(node_move_it_t* it)
{
    for (;it->next; ) {
        var nln = it->next;
        it->next = it->next->npl_list; /* prepare for next iteration in advance */

        var m = nln->npl_node;
        if (node_pair_list_contains(it->info->active_moves, m)
                || node_pair_list_contains(it->info->worklist_moves, m)) {
            return m;
        }
    }
    return NULL;
}


static lv_node_pair_list_t*
node_moves(reg_alloc_info_t* info, lv_node_t* node)
{
    // moveList[n] ∩ (activeMoves ∪ worklistMoves)
    assert(!"TODO");
}

/*
 * Checks whether there is an unprocessed move involving node
 */
static bool
is_move_related(reg_alloc_info_t* info, lv_node_t* node)
{
    // TODO: it might be quicker to simply iterate through the
    // active and worklist and see if node is the src or dst

    node_move_it_t it = {};
    node_move_it_init(&it, info, node);
    var m = node_move_it_next(&it);
    return m != NULL;

    // This is implementation from the book, but it does not think about
    // memory.
    return node_moves(info, node) != NULL;
}

/*
 * An iterator for iterating through the adjacent set of a node
 */
typedef struct adj_it {
    reg_alloc_info_t* info;
    lv_node_list_t* next;
} adj_it_t;

static void
adj_it_init(adj_it_t* it, reg_alloc_info_t* info, lv_node_t* node)
{
    it->info = info;
    it->next = info->adj_list[node->lvn_idx];
}

static lv_node_t*
adj_it_next(adj_it_t* it)
{
    for (;it->next; ) {
        var nln = it->next;
        it->next = it->next->nl_list; /* prepare for next iteration in advance */

        var m = nln->nl_node;
        if (!worklist_contains(it->info, WL_SELECT, m)
                && !worklist_contains(it->info, WL_COALESCED, m)) {
            return m;
        }
    }
    return NULL;
}


/*
 * This moves moves related to node from active_moves to worklist_moves
 * (The implementation in the book obscures the fuck out of this)
 */
static void
enable_moves_node(reg_alloc_info_t* info, lv_node_t* node)
{
    // forall m ∈ NodeMoves(n)
    //   if m ∈ activeMoves then
    //     move m from activeMoves to worklistMoves

    /*
     * step through the active_moves and move nodes to worklist_moves
     * if they relate to our node
     */
    lv_node_t* alias = get_alias(info, node);
    lv_node_pair_list_t** p = &info->active_moves;
    for (; *p; ) {
        lv_node_pair_t* np = (*p)->npl_node;
        if (lv_eq(np->np_node0, alias) || lv_eq(np->np_node1, alias)) {

            var found = *p;
            // unchain from active_moves cell
            *p = found->npl_list;

            // stick onto the front of worklist_moves
            found->npl_list = info->worklist_moves;
            info->worklist_moves = found;

            // in this case we have already changed what *p is to be the
            // next item in active_moves, so we don't execute what
            // would usually be the iteration statement of the for loop.
            // (i.e. what you see in the else).
        } else {
            // adjust p to point to the next cell in the active_moves list
            p = &((*p)->npl_list);
        }
    }
}

/*
 * Enables moves for node m and nodes adjacent to m
 */
static void
enable_moves_adj(reg_alloc_info_t* info, lv_node_t* m)
{
    enable_moves_node(info, m);
    adj_it_t it = {};
    adj_it_init(&it, info, m);
    for (var n = adj_it_next(&it); n; n = adj_it_next(&it)) {
        enable_moves_node(info, n);
    }
}

/*
 *
 */
static void
decrement_degree(reg_alloc_info_t* info, lv_node_t* m)
{
    int d = info->degree[m->lvn_idx];
    if (d == 0) {
        return; // precolored nodes are an example that would hit this path
    }
    info->degree[m->lvn_idx] = d - 1;
    if (d == info->K) {
        // When the degree decrements from K to K - 1 moves associated
        // with its neighbours may be enabled.

        enable_moves_adj(info, m);

        // hack to deal with combine not keeping things consistent
        if ((worklist_contains(info, WL_FREEZE, m) && is_move_related(info, m)) ||
                (worklist_contains(info, WL_SIMPLIFY, m) && !is_move_related(info, m))) {
            return;
        }

        var m_cell = node_list_remove(&info->spill_worklist, m);
        if (is_move_related(info, m_cell->nl_node)) {
            worklist_prepend(info, WL_FREEZE, m_cell);
        } else {
            worklist_prepend(info, WL_SIMPLIFY, m_cell);
        }
    }
}

/*
 * Simplify takes a node, n, from the worklist, pushes it onto
 * the select stack and decrements the degree of each node
 * in the adjacent set of n
 */
static void
simplify(reg_alloc_info_t* info)
{
    // Remove node from simplify_worklist
    // We take the first cell of the list and cons
    // it onto the selectStack instead of actually pulling out the node
    assert(info->simplify_worklist != NULL);

    // remove from simplify_worklist
    var n = info->simplify_worklist->nl_node;
    var n_cell = node_list_remove(&info->simplify_worklist, n);

    // push onto the select_stack
    worklist_prepend(info, WL_SELECT, n_cell);

    adj_it_t it = {};
    adj_it_init(&it, info, n);
    for (var m = adj_it_next(&it); m; m = adj_it_next(&it)) {
        decrement_degree(info, m);
    }
}


// ok to coalesce ?
static bool
ok(reg_alloc_info_t* info, lv_node_t* t, lv_node_t* r)
{
    return info->degree[t->lvn_idx] < info->K
        || worklist_contains(info, WL_PRECOLORED, t)
        || lv_is_adj(t, r);
}

static bool
all_adjacent_ok(reg_alloc_info_t* info, lv_node_t* u, lv_node_t* v)
{
    adj_it_t it = {};
    adj_it_init(&it, info, v);
    for (var t = adj_it_next(&it); t; t = adj_it_next(&it)) {
        if (!ok(info, t, u)) {
            return false;
        }
    }
    return true;
}


/*
 * Works out whether in the union of all the adjacent nodes of u and v
 * there are less than K nodes with degree ≥ K
 */
static bool
conservative_adj(reg_alloc_info_t* info, lv_node_t* u, lv_node_t* v)
{
    Table_T seen = Table_new(0, cmpnode, hashnode);
    int k = 0;

    adj_it_t it = {};
    adj_it_init(&it, info, u);
    for (var n = adj_it_next(&it); n; n = adj_it_next(&it)) {
        Table_put(seen, n, n);
        if (info->degree[n->lvn_idx] >= info->K) {
            k += 1;
        }
    }

    adj_it_init(&it, info, v);
    for (var n = adj_it_next(&it); n; n = adj_it_next(&it)) {
        if (!Table_get(seen, n)) {
            if (info->degree[n->lvn_idx] >= info->K) {
                k += 1;
            }
        }
    }

    Table_free(&seen);
    return (k < info->K);
}


static void
add_work_list(reg_alloc_info_t* info, lv_node_t* u)
{
    if (!worklist_contains(info, WL_PRECOLORED, u)
            && !is_move_related(info, u)
            && info->degree[u->lvn_idx] < info->K)
    {
        var u_cell = node_list_remove(&info->freeze_worklist, u);
        worklist_prepend(info, WL_SIMPLIFY, u_cell);
    }
}

/*
 * increments the degree of u and adds v to it's adj_list
 */
static void
add_edge_helper(reg_alloc_info_t* info, lv_node_t* u, lv_node_t* v)
{
    assert(!worklist_contains(info, WL_PRECOLORED, u));
    // degree is the number of adjacent but not precolored nodes
    info->degree[u->lvn_idx] += 1;

    info->adj_list[u->lvn_idx] =
        list_cons(v, info->adj_list[u->lvn_idx], info->scratch);
}

static void
add_edge(reg_alloc_info_t* info, lv_node_t* u, lv_node_t* v)
{
    if (!lv_is_adj(u, v) && !lv_eq(u, v)) {
        lv_mk_edge(u, v);
        if (!worklist_contains(info, WL_PRECOLORED, u)) {
            add_edge_helper(info, u, v);
        }
        if (!worklist_contains(info, WL_PRECOLORED, v)) {
            add_edge_helper(info, v, u);
        }
    }
}

static void
combine(reg_alloc_info_t* info, lv_node_t* u, lv_node_t* v)
{
    lv_node_list_t* v_cell;
    if (worklist_contains(info, WL_FREEZE, v)) {
        v_cell = node_list_remove(&info->freeze_worklist, v);
    } else {
        v_cell = node_list_remove(&info->spill_worklist, v);
    }

    worklist_prepend(info, WL_COALESCED, v_cell);

    Table_put(info->alias, v, u);

    // The text in the move has some errata:
    // https://www.cs.princeton.edu/~appel/modern/ml/errata99.html p248

    // Combine v's move_list into u's
    for (var m = info->move_list[v->lvn_idx]; m; m = m->npl_list) {
        info->move_list[u->lvn_idx] =
            list_cons(m->npl_node,
                    info->move_list[u->lvn_idx], info->scratch);
    }
    enable_moves_node(info, v);


    adj_it_t it = {};
    adj_it_init(&it, info, v);
    for (var t = adj_it_next(&it); t; t = adj_it_next(&it)) {
        add_edge(info, t, u);
        decrement_degree(info, t);
    }

    if (info->degree[u->lvn_idx] >= info->K
            && worklist_contains(info, WL_FREEZE, u)) {
        var u_cell = node_list_remove(&info->freeze_worklist, u);
        worklist_prepend(info, WL_SPILL, u_cell);
    }
}

/*
 * false processes the worklist of moves attempting to
 * work out if the temporaries involved can be coalesced (assigned the
 * same register)
 */
static void
coalesce(reg_alloc_info_t* info)
{
    assert(info->worklist_moves != NULL);
    var m_cell = info->worklist_moves;

    lv_node_t* x = get_alias(info, m_cell->npl_node->np_node0);
    lv_node_t* y = get_alias(info, m_cell->npl_node->np_node1);

    lv_node_t *u, *v;
    if (worklist_contains(info, WL_PRECOLORED, y)) {
        u = y; v = x;
    } else {
        u = x; v = y;
    }

    // remove from worklist_moves
    info->worklist_moves = info->worklist_moves->npl_list;

    if (lv_eq(u, v)) {
        // add to coalesced_moves
        m_cell->npl_list = info->coalesced_moves;
        info->coalesced_moves = m_cell;
        add_work_list(info, u);
    } else if (worklist_contains(info, WL_PRECOLORED, v) || lv_is_adj(u, v)) {
        m_cell->npl_list = info->constrained_moves;
        info->constrained_moves = m_cell;
        add_work_list(info, u);
        add_work_list(info, v);
    } else {
        bool is_u_precolored = worklist_contains(info, WL_PRECOLORED, u);
        if ((is_u_precolored && all_adjacent_ok(info, u, v))
                || (!is_u_precolored && conservative_adj(info, u, v))) {

            // add to coalesced_moves
            m_cell->npl_list = info->coalesced_moves;
            info->coalesced_moves = m_cell;
            combine(info, u, v);
            add_work_list(info, u);
        } else {
            // add to active_moves
            m_cell->npl_list = info->active_moves;
            info->active_moves = m_cell;
        }
    }
}


static void
freeze_moves(reg_alloc_info_t* info, lv_node_t* u)
{
    node_move_it_t it = {};
    node_move_it_init(&it, info, u);
    for (var m = node_move_it_next(&it); m; m = node_move_it_next(&it)) {
        lv_node_t* x = get_alias(info, m->np_node0);
        lv_node_t* y = get_alias(info, m->np_node1);

        lv_node_t* v;
        if (lv_eq(get_alias(info, y), get_alias(info, u))) {
            v = get_alias(info, x);
        } else {
            v = get_alias(info, y);
        }

        var m_cell = node_pair_list_remove(&info->active_moves, m);
        m_cell->npl_list = info->frozen_moves;
        info->frozen_moves = m_cell;

        if (!is_move_related(info, v) && get_degree(info, v) < info->K) {

            if (worklist_contains(info, WL_FREEZE, v)) {
                var v_cell = node_list_remove(&info->freeze_worklist, v);
                worklist_prepend(info, WL_SIMPLIFY, v_cell);
            }

        }
    }
}


/*
 * I have no idea what this is about, but it's in the book.
 */
static void
freeze(reg_alloc_info_t* info)
{
    var u = info->freeze_worklist->nl_node;
    var u_cell = node_list_remove(&info->freeze_worklist, u);
    worklist_prepend(info, WL_SIMPLIFY, u_cell);
    freeze_moves(info, u);
}


/*
 * We use a spill cost that is the number of uses and defs in the flow
 * graph.
 */
static int
spill_cost(
        reg_alloc_info_t* info,
        lv_node_t* node)
{
    temp_t t = *temp_for_node(info, node);
    int cost = 0;

    var flow = info->flowgraph;
    for (var it = lv_nodes(flow->lvfg_control); lv_node_it_next(&it); ) {
        var node = &it.lvni_node;
        temp_list_t* use_n = Table_get(flow->lvfg_use, node);
        for (var u = use_n; u; u = u->tmp_list) {
            if (u->tmp_temp.temp_id == t.temp_id) {
                cost += 1;
                break;
            }
        }
        temp_list_t* def_n = Table_get(flow->lvfg_def, node);
        for (var d = def_n; d; d = d->tmp_list) {
            if (d->tmp_temp.temp_id == t.temp_id) {
                cost += 1;
                break;
            }
        }
    }
    return cost;
}


static void
select_spill(
        reg_alloc_info_t* info)
{
    // 1. select m from spill_worklist
    // 2. remove m from spill_worklist
    // 3. push m onto simplify_worklist
    // 4. freeze moves

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

    var m_cell = node_list_remove(&info->spill_worklist, m);
    worklist_prepend(info, WL_SIMPLIFY, m_cell);

    freeze_moves(info, m);
}


static void
assign_colors(reg_alloc_info_t* info)
{
    while (info->select_stack != NULL) {
        // pop n from select_stack
        var node = info->select_stack->nl_node;
        var n_cell = node_list_remove(&info->select_stack, node);

        assert(info->K <= 64);
        uint64_t _ok_colors = 0;
        uint64_t* ok_colors = &_ok_colors;
        for (int i = 0; i < info->K; i++) {
            SetBit(ok_colors, i);
        }

        if (debug) {
            fprintf(stderr, "temp=%d\n", temp_for_node(info, node)->temp_id);
        }

        // Remove colors which are already used by adjacent nodes.
        lv_node_list_t* adj = info->adj_list[node->lvn_idx];
        if (debug) { print_adj_list(info, adj); }
        for (var wn = adj; wn; wn = wn->nl_list) {
            var w_alias = get_alias(info, wn->nl_node);
            if (worklist_contains(info, WL_COLORED, w_alias)
                    || worklist_contains(info, WL_PRECOLORED, w_alias)) {
                var w_color = info->color[w_alias->lvn_idx];
                // remove from ok_colors
                ClearBit(ok_colors, w_color);
            }
        }
        if (debug) { print_ok_colors(info, ok_colors); }

        // If we have no remaining colours, spill.
        if (__builtin_popcountll(_ok_colors) == 0) {
            if (debug) { fprintf(stderr, "spill\n"); }
            worklist_prepend(info, WL_SPILLED, n_cell);
        } else {
            // add n to coloured nodes
            worklist_prepend(info, WL_COLORED, n_cell);

            // store the new first available colour for n
            int new_color = __builtin_ctzll(_ok_colors);
            if (debug) { fprintf(stderr, "new_color = %d\n", new_color); }
            info->color[node->lvn_idx] = new_color;
        }
    }

    for (var n = info->coalesced_nodes; n; n = n->nl_list) {
        var node = n->nl_node;
        var alias = get_alias(info, node);
        info->color[node->lvn_idx] = info->color[alias->lvn_idx];
    }
}


static void
debug_print_degrees(reg_alloc_info_t* info, int count_nodes)
{
    fprintf(stderr, "len(precolored) = %d\n", list_length(info->precolored));
    fprintf(stderr, "len(initial) = %d\n", list_length(info->initial));

    fprintf(stderr, "degree = [");
    for (int i = 0; i < count_nodes; i++) {
        fprintf(stderr, "%d,", info->degree[i]);
    }
    fprintf(stderr, "]\n");
}


static void
check_invariants(reg_alloc_info_t* info)
{
    // Degree invariant:
    // u ∈ simplify_worklist ∪ freeze_worklist ∪ spill_worklist ⇒
    //  degree(u) = |adj_list(u) ∩ (precolored ∪ simplify_worklist
    //                              ∪ freeze_worklist ∪ spill_worklist)|

    lv_node_list_t* worklists[] = {
        info->simplify_worklist, info->freeze_worklist, info->spill_worklist
    };
    const int n = sizeof worklists / sizeof(void*);

    for (int i = 0; i < n; i++) {
        for (var u_cell = worklists[i]; u_cell; u_cell = u_cell->nl_list) {
            var u = u_cell->nl_node;
            var d = info->degree[u->lvn_idx];

            int num_adj = 0;
            adj_it_t it = {};
            adj_it_init(&it, info, u);
            for (var n = adj_it_next(&it); n; n = adj_it_next(&it)) {
                num_adj += 1;
            }
            assert(d == num_adj);
        }
    }


    // Simplify worklist invariant:
    // u ∈ simplify_worklist ⇒
    //  degree(u) < K ∧ move_list[u] ∩ (active_moves ∪ worklist_moves) = ø
    for (var u_cell = info->simplify_worklist; u_cell; u_cell = u_cell->nl_list) {
        var u = u_cell->nl_node;
        assert(info->degree[u->lvn_idx] < info->K || !is_move_related(info, u));
    }

    // Freeze worklist invariant:
    // u ∈ freeze_worklist ⇒
    //  degree(u) < K ∧ move_list[u] ∩ (active_moves ∪ worklist_moves) ≠ ø

    for (var u_cell = info->freeze_worklist; u_cell; u_cell = u_cell->nl_list) {
        var u = u_cell->nl_node;
        assert(info->degree[u->lvn_idx] < info->K || is_move_related(info, u));
    }

    // Spill worklist invariant:
    // u ∈ spill_worklist ⇒ degree(u) ≥ K

    for (var u_cell = info->spill_worklist; u_cell; u_cell = u_cell->nl_list) {
        var u = u_cell->nl_node;
        assert(info->degree[u->lvn_idx] >= info->K);
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
    const char* registers[],
    Arena_T ar_spills,
    Arena_T ar_allocation
    )
{
    // Prepare
    reg_alloc_info_t info = {
        .K = Table_length(initial_allocation),
        .flowgraph = flowgraph,
        .interference = interference,
        .scratch = Arena_new(),
    };
#define Salloc(nbytes) Arena_alloc(info.scratch, nbytes, __FILE__, __LINE__)

    var count_nodes = Table_length(interference->lvig_gtemp);
    // Attention: all pointers to nodes which are stored, must be pointers
    // into this nodes array to avoid dangles / observed mutation
    lv_node_t* nodes = Salloc(count_nodes * sizeof *nodes);
    for (var it = lv_nodes(interference->lvig_graph); lv_node_it_next(&it); ) {
        nodes[it.lvni_node.lvn_idx] = it.lvni_node;
    }
    info.degree = Salloc(count_nodes * sizeof *info.degree);
    info.color = Salloc(count_nodes * sizeof *info.color);
    // This is correct, since we are allocating an array of pointers
    // NOLINTNEXTLINE(bugprone-sizeof-expression)
    info.adj_list = Salloc(count_nodes * sizeof *info.adj_list);
    // NOLINTNEXTLINE(bugprone-sizeof-expression)
    info.move_list = Salloc(count_nodes * sizeof *info.move_list);
    info.alias = Table_new(0, cmpnode, hashnode);
    info.worklist = Salloc(count_nodes * sizeof *info.worklist);
#undef Salloc


    for (int i = 0; i < count_nodes; i++) {
        var node = &nodes[i];

        temp_t* t = temp_for_node(&info, node);

        var is_precolored = !!Table_get(initial_allocation, t);
        if (is_precolored) {
            info.precolored = list_cons(node, info.precolored, info.scratch);
            info.color[node->lvn_idx] = t->temp_id;
        } else {
            info.initial = list_cons(node, info.initial, info.scratch);
            info.worklist[node->lvn_idx] = WL_INITIAL;

            for (int j = 0; j < count_nodes; j++) {
                var m = &nodes[j];
                // todo: write an efficient adj iterator
                if (lv_is_adj(node, m)) {
                    add_edge_helper(&info, node, m);
                }
            }
        }
    }

    /*
     * Construct the move_list
     */
    for (var m = interference->lvig_moves; m; m = m->npl_list) {
        // The move is added to the move list for both the target and dest
        var node_pair = m->npl_node;
        info.move_list[node_pair->np_node0->lvn_idx] =
            list_cons(node_pair,
                    info.move_list[node_pair->np_node0->lvn_idx], info.scratch);
        info.move_list[node_pair->np_node1->lvn_idx] =
            list_cons(node_pair,
                    info.move_list[node_pair->np_node1->lvn_idx], info.scratch);
    }

    // It's a bit cheeky but we can use lvig_moves for our move worklist
    if (enable_coalescing) {
        info.worklist_moves = interference->lvig_moves;
    }

    if (debug) { debug_print_degrees(&info, count_nodes); }

    /*
     * :: MakeWorklist ::
     */
    for (var n = info.initial; n; n = info.initial) {
        var node = n->nl_node;
        if (info.degree[node->lvn_idx] >= info.K) {
            // add to spill_worklist
            info.initial = n->nl_list;
            worklist_prepend(&info, WL_SPILL, n);
        } else if (enable_coalescing && is_move_related(&info, node)) {
            // add to freeze_worklist
            info.initial = n->nl_list;
            worklist_prepend(&info, WL_FREEZE, n);
        } else {
            // add to simplify_worklist
            info.initial = n->nl_list;
            worklist_prepend(&info, WL_SIMPLIFY, n);
        }
    }

    /*
     * The loop before "AssignColors", from "Main"
     */
    while (info.simplify_worklist || info.worklist_moves
            || info.freeze_worklist || info.spill_worklist) {
        if (debug) { check_invariants(&info); }

        if (info.simplify_worklist) {
            simplify(&info);
        } else if (info.worklist_moves) {
            coalesce(&info);
        } else if (info.freeze_worklist) {
            freeze(&info);
        } else {
            select_spill(&info);
        }
    }

    if (debug) { print_coalesced_nodes(&info); }

    assign_colors(&info);

    // Next to do! convert our colouring and spills into
    // the correct result structure.

    struct ra_color_result result = {};

    // Return Spills
    for (var n = info.spilled_nodes; n; n = n->nl_list) {
        var node = n->nl_node;
        temp_t* temp = temp_for_node(&info, node);
        result.racr_spills =
            temp_list_cons(*temp, result.racr_spills, ar_spills);
    }

    // Return Allocation
    result.racr_allocation = Table_new(0, cmptemp, hashtemp);
    for (var n = info.precolored; n; n = n->nl_list) {
        var node = n->nl_node;
        var temp = temp_copy_to_arena(
                ar_allocation, *temp_for_node(&info, node));
        Table_put(result.racr_allocation,
                temp,
                Table_get(initial_allocation, temp));
    }
    for (var n = info.colored_nodes; n; n = n->nl_list) {
        var node = n->nl_node;

        var color_idx = info.color[node->lvn_idx];
        var register_name = registers[color_idx];

        Table_put(
                result.racr_allocation,
                temp_copy_to_arena(ar_allocation, *temp_for_node(&info, node)),
                // cast away const
                (void*)register_name);
    }
    for (var n = info.coalesced_nodes; n; n = n->nl_list) {
        var node = n->nl_node;
        var alias = get_alias(&info, node);
        if (worklist_contains(&info, WL_SPILLED, alias)) {
            continue;
        }

        var color_idx = info.color[node->lvn_idx];
        var register_name = registers[color_idx];

        Table_put(
                result.racr_allocation,
                temp_copy_to_arena(ar_allocation, *temp_for_node(&info, node)),
                // cast away const
                (void*)register_name);
    }

    if (debug) { debug_print_degrees(&info, count_nodes); }

    // :: clean up ::

    // TODO: move all the moves from the various worklists back to the
    // lvig_moves list

    assert(info.initial == NULL);
    assert(info.simplify_worklist == NULL);
    assert(info.spill_worklist == NULL);

    // dealloc all adj_lists
    Table_free(&info.alias);
    Arena_dispose(&info.scratch);
    return result;
}

static void
replace_temp(temp_list_t* temp_list, temp_t to_be_replaced, temp_t replacement)
{
    for (var t = temp_list; t; t = t->tmp_list) {
        if (temp_eq(t->tmp_temp, to_be_replaced)) {
            t->tmp_temp = replacement;
        }
    }
}

static void
spill_temp(
        Arena_T ar_instrs, // arena for body_instrs
        Arena_T ar_frags, // arena for fragments
        temp_state_t* temp_state,
        ac_frame_t* frame,
        assm_instr_t** pbody_instrs,
        temp_t temp_to_spill
        )
{
    if (debug) {
        fprintf(stderr, "spilling temp: %d\n", temp_to_spill.temp_id);
    }
    /*
     * To think about:
     *   if temp_to_spill is already one of our function's local variables
     *   then we could spill it without allocating more stack space...
     */

    var backend = frame->acf_target->tgt_backend;

    struct ac_frame_var* new_frame_var =
        ac_spill_temporary(frame, temp_to_spill, ar_frags);
    for (var pinstr = pbody_instrs; *pinstr; pinstr = &((*pinstr)->ai_list)) {
        var instr = *pinstr;
        switch (instr->ai_tag) {
            case ASSM_INSTR_OPER:
                if (temp_list_contains(instr->ai_oper_dst, temp_to_spill)) {
                    // Want to store to our new stack location
                    // after
                    var new_temp = temp_newtemp(temp_state,
                            temp_to_spill.temp_size, temp_to_spill.temp_ptr_dispo);
                    replace_temp(instr->ai_oper_dst, temp_to_spill, new_temp);
                    var new_instr = backend->store_temp(
                            new_frame_var, new_temp, ar_instrs);

                    // HACK to know spilled registers in stack maps
                    assert(new_frame_var->acf_stored.temp_id == -1);
                    new_frame_var->acf_stored = new_temp;

                    // graft in
                    new_instr->ai_list = instr->ai_list;
                    instr->ai_list = new_instr;
                }
                if (temp_list_contains(instr->ai_oper_src, temp_to_spill)) {
                    // Want to fetch from our new stack location
                    // before
                    var new_temp = temp_newtemp(temp_state,
                            temp_to_spill.temp_size, temp_to_spill.temp_ptr_dispo);
                    replace_temp(instr->ai_oper_src, temp_to_spill, new_temp);
                    var new_instr = backend->load_temp(
                            new_frame_var, new_temp, ar_instrs);
                    // graft in
                    new_instr->ai_list = instr;
                    *pinstr = new_instr;
                }
                break;
            case ASSM_INSTR_LABEL:
                break;
            case ASSM_INSTR_MOVE:
                if (temp_eq(instr->ai_move_dst, temp_to_spill)) {
                    // Want to store to our new stack location
                    // after
                    var new_temp = temp_newtemp(temp_state,
                            temp_to_spill.temp_size, temp_to_spill.temp_ptr_dispo);
                    instr->ai_move_dst = new_temp;
                    var new_instr = backend->store_temp(
                            new_frame_var, new_temp, ar_instrs);
                    /*
                     * Hack to know what register was spilled when creating
                     * stack maps.
                     */
                    assert(new_frame_var->acf_stored.temp_id == -1);
                    new_frame_var->acf_stored = new_temp;

                    // graft in
                    new_instr->ai_list = instr->ai_list;
                    instr->ai_list = new_instr;
                }
                if (temp_eq(instr->ai_move_src, temp_to_spill)) {
                    // Want to fetch from our new stack location
                    // before
                    var new_temp = temp_newtemp(temp_state,
                            temp_to_spill.temp_size, temp_to_spill.temp_ptr_dispo);
                    instr->ai_move_src = new_temp;
                    var new_instr = backend->load_temp(
                            new_frame_var, new_temp, ar_instrs);
                    // graft in
                    new_instr->ai_list = instr;
                    *pinstr = new_instr;
                }
                break;
        }
    }
}


/*
 * Finds moves between the same registers and removes them
 */
static void
remove_dead_moves(
        Table_T allocation, /* temp_t* -> register (char*) */
        assm_instr_t** pbody_instrs)
{
    for (var pinstr = pbody_instrs; *pinstr; ) {
        var instr = *pinstr;
        if (instr->ai_tag == ASSM_INSTR_MOVE) {
            var dst = instr->ai_move_dst;
            var src = instr->ai_move_src;
            if (dst.temp_size == src.temp_size) {
                const char* dst_reg = Table_get(allocation, &dst);
                assert(dst_reg);
                const char* src_reg = Table_get(allocation, &src);
                assert(src_reg);
                if (dst_reg == src_reg) {

                    // Remove from body_instrs
                    *pinstr = instr->ai_list;

                    instr->ai_list = NULL;
                    continue;
                }
            }
        }

        pinstr = &((*pinstr)->ai_list);
    }
}


static const char*
temp_dispo_str(temp_t t)
{
    switch (t.temp_ptr_dispo) {
        case TEMP_DISP_PTR: return "*";
        case TEMP_DISP_NOT_PTR: return "";
        case TEMP_DISP_INHERIT: return "^";
    }
    return "!!!!!!!";
}

static bool
is_call_instr(assm_instr_t* instr)
{
    /*
     * We abuse the knowledge that we have put Lret labels for the
     * instruction after each call.
     */
    return instr->ai_list
        && instr->ai_list->ai_tag == ASSM_INSTR_LABEL
        && strstr(instr->ai_list->ai_label, "Lret");
}

/*
 * Work out the liveness of any temporary that is spilled, before it is
 * spilled, at each call instruction.
 * This will be used as the liveness of frame slot where it is spilled to.
 * This will seem more useful on x86_64 where there are more spills with
 * shorter lifetimes.
 */
static void
record_spill_liveness(
        assm_instr_t* instrs,
        lv_flowgraph_t* flowgraph,
        struct igraph_and_table igraph_and_table,
        temp_list_t* about_to_spill,
        Table_T label_to_spill_liveness, // sl_sym_t -> temp_list_t*
        Arena_T ar_spill_liveness
        )
{

    var it = lv_nodes(flowgraph->lvfg_control); lv_node_it_next(&it);
    for (var instr = instrs; instr && lv_node_it_next(&it); instr = instr->ai_list) {
        if (is_call_instr(instr)) {
            temp_list_t* live_outs =
                Table_get(igraph_and_table.live_outs, &it.lvni_node);

            temp_list_t* spill_live_outs =
                Table_get(label_to_spill_liveness, instr->ai_list->ai_label);

            temp_list_t* updated = spill_live_outs;

            // If only we had written an efficient set implementation
            for (var tl0 = live_outs; tl0; tl0 = tl0->tmp_list) {
                if (temp_list_contains(updated, tl0->tmp_temp)) {
                    continue;
                }
                for (var tl1 = about_to_spill; tl1; tl1 = tl1->tmp_list) {
                    if (tl0->tmp_temp.temp_id == tl1->tmp_temp.temp_id) {

                        updated = temp_list_cons(tl0->tmp_temp, updated,
                                ar_spill_liveness);

                    }
                }
            }

            Table_put(label_to_spill_liveness, instr->ai_list->ai_label,
                    updated);
        }
    }
}

/*
 * Here cs_idx is the index of the register when they are put
 * in an array, in order. See callee_saved in runtime/runtime.c
 */
static void
set_cs_bitmap(
        uint32_t* cs_bitmap, int cs_idx, temp_ptr_disposition_t dispo)
{
    uint32_t value = -1;
    switch (dispo) {
        case TEMP_DISP_NOT_PTR: value = 0b00; break;
        case TEMP_DISP_PTR:     value = 0b01; break;
        case TEMP_DISP_INHERIT: value = 0b10; break;
    }
    assert(value != -1);

    // a mask of 1s except where value will go
    uint32_t mask = 0xFFFFFFFFUL ^ (0b11 << (2*cs_idx));
    *cs_bitmap = (*cs_bitmap & mask) | (value << (2*cs_idx));
}

/*
 * Work out the type of the data contained in the callee saved registers
 * across call instructions to help a garbage collector find roots.
 */
static void
compute_cs_ptr_dispo_at_call_sites(
        ac_frame_t* frame,
        assm_instr_t* instrs,
        lv_flowgraph_t* flowgraph,
        struct igraph_and_table igraph_and_table,
        Table_T allocation, // temp_t* -> register (char*)
        Table_T label_to_cs_bitmap // sl_sym_t -> uint32_t
        )
{
    const target_t* target = frame->acf_target;
    const int csn = target->callee_saves.length;
    const char* callee_save_names[target->callee_saves.length];
    for (int i = 0, n = target->callee_saves.length; i < n; i++) {
        callee_save_names[i] =
            target->register_names[target->callee_saves.elems[i].temp_id];
    }

    var it = lv_nodes(flowgraph->lvfg_control); lv_node_it_next(&it);
    for (var instr = instrs; instr && lv_node_it_next(&it); instr = instr->ai_list) {
        if (is_call_instr(instr)) {
            temp_list_t* live_outs =
                Table_get(igraph_and_table.live_outs, &it.lvni_node);

            // Any non-live callee saved will get a value of 0b00.
            uint32_t cs_bitmap = 0;

            for (var tl = live_outs; tl; tl = tl->tmp_list) {
                const char* reg = Table_get(allocation, &(tl->tmp_temp));
                var t = tl->tmp_temp;
                for (int i = 0; i < csn; i++) {
                    // Safe comparison because these come from the same place.
                    if (reg == callee_save_names[i]) {
                        if (debug) {
                            fprintf(stderr,
                                    "%s (t%d%s) is live across call %s\n",
                                    reg, t.temp_id, temp_dispo_str(t),
                                    instr->ai_list->ai_label);
                        }
                        set_cs_bitmap(&cs_bitmap, i, t.temp_ptr_dispo);
                    }
                }
            }

            // Table_T only accepts non-zero pointers as values.
            union { uint32_t i32; void* v; } table_value = {.i32=cs_bitmap};
            Table_put(label_to_cs_bitmap, instr->ai_list->ai_label,
                    table_value.v);
        }
    }
}

static void
debug_print_instrs(assm_instr_t* body_instrs, ac_frame_t* frame)
{
    for (var i = body_instrs; i; i = i->ai_list) {
        char buf[128];
        assm_format(buf, 128, i, frame->acf_temp_map, frame->acf_target);
        fprintf(stderr, "%s", buf);
    }
}

/*
 * Performs liveness analysis and register allocation.
 *
 * body_instrs is no longer valid after calling this; it is mutated and
 * returned as ra_instrs in the returned structure.
 */
struct instr_list_and_allocation
ra_alloc(
        FILE* out,
        temp_state_t* temp_state,
        assm_instr_t* body_instrs,
        ac_frame_t* frame,
        bool print_interference_and_return,
        Table_T label_to_cs_bitmap,
        Table_T label_to_spill_liveness,
        Arena_T arena_spill_liveness, // TODO: consolidate if possible
        Arena_T arena_instrs,
        Arena_T arena_allocation,
        Arena_T arena_fragments)
{
    // liveness analysis
    var scratch = Arena_new();
    var flow_and_nodes = instrs2graph(body_instrs, scratch);
    lv_flowgraph_t* flow = flow_and_nodes.flowgraph;
    if (print_interference_and_return) {
        flowgraph_print(out, flow, flow_and_nodes.node_list, body_instrs,
                frame->acf_target);
    }

    var igraph_and_table =
        interference_graph(flow, flow_and_nodes.node_list, scratch);
    if (print_interference_and_return) {
        igraph_show(out, igraph_and_table.igraph);
        lv_free_interference_and_flow_graph(&igraph_and_table, &flow_and_nodes);
        Arena_dispose(&scratch);
        return (struct instr_list_and_allocation) { .ra_instrs = body_instrs };
    }

    // register allocation
    var color_result =
        ra_color(igraph_and_table.igraph, flow, frame->acf_temp_map,
                frame->acf_target->register_names, scratch,
                arena_allocation);

    // :: check for spilled nodes, and rewrite program if so ::
    if (color_result.racr_spills) {
        if (debug) {fprintf(stderr, "spilling!\n");}
        if (debug) {
            fprintf(stderr, "# before rewrite:\n");
            debug_print_instrs(body_instrs, frame);
        }

        record_spill_liveness(body_instrs, flow, igraph_and_table,
                color_result.racr_spills, label_to_spill_liveness,
                arena_spill_liveness);

        for (temp_list_t* x = color_result.racr_spills; x; x = x->tmp_list) {
            spill_temp(arena_instrs, arena_fragments, temp_state,
                    frame, &body_instrs, x->tmp_temp);
        }

        if (debug) {
            fprintf(stderr, "# rewritten:\n");
            debug_print_instrs(body_instrs, frame);
        }

        Table_free(&color_result.racr_allocation);

        lv_free_interference_and_flow_graph(&igraph_and_table, &flow_and_nodes);
        Arena_dispose(&scratch);
        return ra_alloc(out, temp_state, body_instrs, frame, false,
                label_to_cs_bitmap, label_to_spill_liveness,
                arena_spill_liveness, arena_instrs, arena_allocation,
                arena_fragments);
    }

    /* Must happen before removing the dead moves, so that flowgraph
     * nodes line up with instructions.
     */
    compute_cs_ptr_dispo_at_call_sites(frame, body_instrs, flow,
            igraph_and_table, color_result.racr_allocation, label_to_cs_bitmap);

    remove_dead_moves(color_result.racr_allocation, &body_instrs);

    struct instr_list_and_allocation result = {
        .ra_instrs = body_instrs,
        .ra_allocation = color_result.racr_allocation
    };

    lv_free_interference_and_flow_graph(&igraph_and_table, &flow_and_nodes);
    Arena_dispose(&scratch);

    return result;
}
