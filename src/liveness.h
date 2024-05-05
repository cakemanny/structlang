#ifndef __LIVENESS_H__
#define __LIVENESS_H__
// vim:ft=c:

#include <stdbool.h>
#include <stdint.h>
#include "interfaces/arena.h"
#include "interfaces/table.h"
#include "assem.h" // assm_instr_t

// TODO: define wrapper types around these graphs to improve type safety.

/*
 * Graph
 */
typedef struct lv_graph_t lv_graph_t;

typedef struct lv_node_t {
    lv_graph_t* lvn_graph;
    size_t lvn_idx;
} lv_node_t;

typedef struct lv_node_list_t {
    lv_node_t* nl_node;
    struct lv_node_list_t* nl_list;
} lv_node_list_t;

// Node iterator, array backing
typedef struct {
    lv_node_t lvni_node;
    const void* node_array;
    lv_graph_t* graph;
    int i;
} lv_node_it_arr;

typedef struct {
    lv_node_t lvni_node;
    const void* node_array[2];
    int i[2];
    int j;
} lv_node_it_2arr;

// Node iterator, virtual backing
typedef struct {
    lv_node_t lvni_node;
    int i;
} lv_node_it_virt;

extern bool _lv_node_it_arr_next(lv_node_it_arr*);
extern bool _lv_node_it_2arr_next(lv_node_it_2arr*);
extern bool _lv_node_it_virt_next(lv_node_it_virt* it);
#define lv_node_it_next(it) _Generic((it) \
        , lv_node_it_arr*: _lv_node_it_arr_next \
        , lv_node_it_2arr*: _lv_node_it_2arr_next \
        , lv_node_it_virt*: _lv_node_it_virt_next \
        )(it)

extern lv_node_it_virt lv_nodes(lv_graph_t*);
extern lv_node_it_arr lv_succ(lv_node_t*);
extern lv_node_it_arr lv_pred(lv_node_t*);
extern lv_node_it_2arr lv_adj(lv_node_t*);
extern bool lv_is_adj(const lv_node_t*, const lv_node_t*);
extern void lv_node_list_free(lv_node_list_t*);

static inline bool
lv_eq(const lv_node_t* a, const lv_node_t* b)
{
    return a->lvn_graph == b->lvn_graph && a->lvn_idx == b->lvn_idx;
}

extern lv_graph_t* lv_new_graph(Arena_T);
extern size_t lv_graph_length(const lv_graph_t*);
extern lv_node_t* lv_new_node(lv_graph_t* graph, Arena_T);
extern void lv_mk_edge(lv_node_t* from, lv_node_t* to);
extern void lv_rm_edge(lv_node_t* from, lv_node_t* to);

extern void lv_print_graph(lv_graph_t*);

/* for debugging */
char* lv_nodename(lv_node_t* node);

// 4-way hash trie
typedef struct lv_node_temps_map_t {
    struct lv_node_temps_map_t *child[4];
    lv_node_t*  key;
    temp_list_t*  value;
} lv_node_temps_map_t;

temp_list_t* *nt_upsert(lv_node_temps_map_t **m, lv_node_t* key, Arena_T);
temp_list_t* nt_get(lv_node_temps_map_t *m, lv_node_t* key);

uint32_t lv_node_hash(lv_node_t* n);

// nodeset_t is a simple add-only set.
typedef struct nodeset_t nodeset_t;
bool nodeset_upsert(nodeset_t **m, lv_node_t* key, Arena_T arena);
bool nodeset_ismember(nodeset_t* m, lv_node_t* key);

/*
 * Flow
 */
typedef struct lv_flowgraph_t lv_flowgraph_t;

struct lv_flowgraph_t {
    lv_graph_t* lvfg_control;
    lv_node_temps_map_t* lvfg_def; // does not own keys
    lv_node_temps_map_t* lvfg_use; // does not own keys
    nodeset_t* lvfg_ismove;
};

/*
 * MakeGraph
 */
struct flowgraph_and_node_list {
    lv_flowgraph_t* flowgraph;
    lv_node_list_t* node_list;
} instrs2graph(const assm_instr_t*, Arena_T);

/*
 * Liveness
 */
typedef struct lv_node_pair_t {
    lv_node_t* np_node0;
    lv_node_t* np_node1;
} lv_node_pair_t;

typedef struct lv_node_pair_list_t {
    lv_node_pair_t* npl_node;
    struct lv_node_pair_list_t* npl_list;
} lv_node_pair_list_t;

lv_node_pair_t* lv_node_pair(lv_node_t* m, lv_node_t* n, Arena_T);
lv_node_pair_list_t* lv_node_pair_cons(
        lv_node_pair_t* hd, lv_node_pair_list_t* tl, Arena_T);
bool lv_node_pair_eq(const lv_node_pair_t* lhs, const lv_node_pair_t* rhs);

// an interference graph
typedef struct lv_igraph_t lv_igraph_t;
struct lv_igraph_t {
    lv_graph_t* lvig_graph;

    /*
     * lvig_tnode is a map from temp_t* -> lv_node_t*
     * It gives the interference graph node for a given register or temporary
     */
    Table_T lvig_tnode; // should be a function?
    /*
     * lvig_gtemp is a map from lv_node_t* -> temp_t*
     */
    Table_T lvig_gtemp;
    /*
     * lvig_moves is a list of node pairs ((d0, s0), (d1, s2), ...)
     * for each move in the program, with dₙ, sₙ being interference graph
     * nodes for the temporaries involved in the move.
     */
    lv_node_pair_list_t* lvig_moves;
};

struct igraph_and_table {
    lv_igraph_t* igraph; // The interference graph
    /*
     * live_outs is the mapping from flow graph nodes to the temporaries
     * that are Live Out at that node
     */
    lv_node_temps_map_t* live_outs;
};

struct igraph_and_table interference_graph(
        lv_flowgraph_t*, lv_node_list_t* cg_nodes, Arena_T);

void lv_free_interference_and_flow_graph(
        struct igraph_and_table*, struct flowgraph_and_node_list*);

#include <stdio.h>

void
flowgraph_print(
        FILE* out, const lv_flowgraph_t* flow_graph,
        const lv_node_list_t* node_list, const assm_instr_t* instrs,
        const target_t* target);

/*
 * The function igraph_show just prints out - for debugging purposes - a list
 * of nodes in the interference graph, and for each node, a list of nodes
 * adjacent to it.
 */
void igraph_show(FILE* out, lv_igraph_t* igraph);


#endif /* __LIVENESS_H__ */
