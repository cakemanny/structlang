#include <stddef.h>
#include <assert.h>
#include "liveness.h"
#include "mem.h"
#include "list.h"

#define var __auto_type


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

lv_flowgraph_t* instrs2graph(assm_instr_t* instrs)
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
                    Table_put(def, node, instr->ai_oper_dst);
                }
                if (instr->ai_oper_src) {
                    Table_put(use, node, instr->ai_oper_src);
                }
                break;
            case ASSM_INSTR_LABEL:
                // in this case we don't automatically make an edge from the
                // previous node
                // since we want to look at jumps to decide about that, let's
                // defer this case until we've built all our nodes
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

    return flow_graph;
}
