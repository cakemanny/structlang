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

// todo: lv_node_list_t* lv_nodes(lv_graph_t*)
// todo: succ
// todo: pred
// todo: adj
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
// this should also return a node list...
lv_flowgraph_t* instrs2graph(assm_instr_t*);

#endif /* __LIVENESS_H__ */
