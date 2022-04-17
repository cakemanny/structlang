#ifndef __LIVENESS_H__
#define __LIVENESS_H__
// vim:ft=c:

#include "assem.h" // assm_instr_t
#include "interfaces/table.h"

/*
 * Graph
 */
typedef struct lv_graph_t lv_graph_t;
typedef struct lv_node_t lv_node_t;

/* this might not be correct wrt Rust rep  */
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
extern _Bool lv_eq(const lv_node_t*, const lv_node_t*);

// I'm putting extern here for functions implemented in Rust
extern lv_graph_t* lv_new_graph();
extern void lv_free_graph(lv_graph_t*);
extern lv_node_t* lv_new_node(lv_graph_t* graph);
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
    Table_T lvfg_def; // node -> temp_list_t
    Table_T lvfg_use; // node -> temp_list_t
    Table_T lvfg_ismove; // node -> bool
};

/*
 * MakeGraph
 */
struct flowgraph_and_node_list {
    lv_flowgraph_t* flowgraph;
    lv_node_list_t* node_list;
} instrs2graph(const assm_instr_t*);

/*
 * Liveness
 */
typedef struct lv_node_pair_t {
    lv_node_t* np_node0;
    lv_node_t* np_node1;
} lv_node_pair_t;

typedef struct lv_node_pair_list_t {
    lv_node_pair_t* nl_node;
    struct lv_node_list_t* nl_list;
} lv_node_pair_list_t;

lv_node_pair_t* lv_node_pair(lv_node_t* m, lv_node_t* n);
lv_node_pair_list_t* lv_node_pair_cons(
        lv_node_pair_t* hd, lv_node_pair_list_t* tl);

// an interference graph
typedef struct lv_igraph_t lv_igraph_t;
struct lv_igraph_t {
    lv_graph_t* lvig_graph;
    Table_T lvig_tnode; // should be a function?
    Table_T lvig_gtemp; // should be a function?
    lv_node_pair_list_t* lvig_moves;
};

struct igraph_and_table {
    lv_igraph_t* igraph;
    Table_T live_outs;
} intererence_graph(lv_flowgraph_t*);

#include <stdio.h>
/*
 * The function igraph_show just prints out - for debugging purposes - a list
 * of nodes in the interference graph, and for each node, a list of nodes
 * adjacent to it.
 */
void igraph_show(FILE* out, lv_igraph_t* igraph);


#endif /* __LIVENESS_H__ */
