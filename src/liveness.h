#ifndef __LIVENESS_H__
#define __LIVENESS_H__
// vim:ft=c:

#include <stdbool.h>
#include "interfaces/arena.h"
#include "interfaces/table.h"
#include "assem.h" // assm_instr_t

// TODO: define wrapper types around these graphs to improve type safety.

/*
 * Graph
 */
typedef struct lv_graph_t lv_graph_t;
typedef struct lv_node_t lv_node_t;

struct lv_node_t {
    lv_graph_t* lvn_graph;
    size_t lvn_idx;
};

typedef struct lv_node_list_t {
    lv_node_t* nl_node;
    struct lv_node_list_t* nl_list;
} lv_node_list_t;


extern lv_node_list_t* lv_nodes(lv_graph_t*);
extern lv_node_list_t* lv_succ(lv_node_t*);
extern lv_node_list_t* lv_pred(lv_node_t*);
extern lv_node_list_t* lv_adj(lv_node_t*);
extern void lv_node_list_free(lv_node_list_t*);
extern bool lv_eq(const lv_node_t*, const lv_node_t*);
extern bool lv_is_adj(const lv_node_t*, const lv_node_t*);

typedef struct node_vec {
    int *nv_elems;
    int nv_len;
    int nv_capacity;
} node_vec_t;
extern node_vec_t lv_succ_vec(lv_node_t*);

extern lv_graph_t* lv_new_graph();
extern size_t lv_graph_length(const lv_graph_t*);
extern void lv_free_graph(lv_graph_t*);
extern lv_node_t* lv_new_node(lv_graph_t* graph);
/* for when freeing a node that came from lv_new_node */
extern void lv_free_node(lv_node_t*);
extern void lv_mk_edge(lv_node_t* from, lv_node_t* to);
extern void lv_rm_edge(lv_node_t* from, lv_node_t* to);

extern void lv_print_graph(lv_graph_t*);

/* for debugging */
char* lv_nodename(lv_node_t* node);

/*
 * Flow
 */
typedef struct lv_flowgraph_t lv_flowgraph_t;

struct lv_flowgraph_t {
    lv_graph_t* lvfg_control;
    Table_T lvfg_def; // node -> temp_list_t  does not own keys
    Table_T lvfg_use; // node -> temp_list_t  does not own keys
    Table_T lvfg_ismove; // node -> bool
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
    Table_T live_outs; // lv_node_t* -> temp_list_t*
};

struct igraph_and_table interference_graph(
        lv_flowgraph_t*, lv_node_list_t* node_list, Arena_T);

void lv_free_interference_and_flow_graph(
        struct igraph_and_table*, struct flowgraph_and_node_list*);

#include <stdio.h>
/*
 * The function igraph_show just prints out - for debugging purposes - a list
 * of nodes in the interference graph, and for each node, a list of nodes
 * adjacent to it.
 */
void igraph_show(FILE* out, lv_igraph_t* igraph);


#endif /* __LIVENESS_H__ */
