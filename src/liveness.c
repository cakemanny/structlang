#include <stddef.h>
#include <assert.h>
#include <string.h>
#include "liveness.h"
#include "mem.h"
#include "list.h"

#define var __auto_type

#ifdef NDEBUG
static _Bool debug = 0;
#else
static _Bool debug = 1;
#endif

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

    typeof(instrs) prev = NULL;
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

// sorts a temp list, in place...
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

    // write the elements back to the list
    i = 0;
    for (var t = tl; t; t = t->tmp_list, i++) {
        memcpy(&t->tmp_temp, temp_array + i, sizeof t->tmp_temp);
    }
    return tl;
}

static void check_sorted(const temp_list_t* tl)
{
    for (var t = tl; t && t->tmp_list; t = t->tmp_list) {
        assert(cmptemp(&t->tmp_temp, &t->tmp_list->tmp_temp) < 0);
    }
}

static temp_list_t* temp_list_union(const temp_list_t* a, const temp_list_t* b)
{
    if (debug) {
        check_sorted(a);
        check_sorted(b);
    }

    temp_list_t* result = NULL;

    while (a || b) {
        if (a && b) {
            var cmp = cmptemp(&a->tmp_temp, &b->tmp_temp);
            if (cmp < 0) {
                result = temp_list_cons(a->tmp_temp, result);
                a = a->tmp_list;
            } else if (cmp > 0) {
                result = temp_list_cons(b->tmp_temp, result);
                b = b->tmp_list;
            } else {
                result = temp_list_cons(a->tmp_temp, result);
                a = a->tmp_list;
                b = b->tmp_list;
            }
        } else if (a) {
            if (!result || cmptemp(&a->tmp_temp, &result->tmp_temp) != 0) {
                result = temp_list_cons(a->tmp_temp, result);
            }
            a = a->tmp_list;
        } else {
            assert(b);
            if (!result || cmptemp(&b->tmp_temp, &result->tmp_temp) != 0) {
                result = temp_list_cons(b->tmp_temp, result);
            }
            b = b->tmp_list;
        }
    }

    return list_reverse(result);
}

static temp_list_t* temp_list_minus(const temp_list_t* a, const temp_list_t* b)
{
    if (debug) {
        check_sorted(a);
        check_sorted(b);
    }

    temp_list_t* result = NULL;

    for (; a; a = a->tmp_list) {

        // advance b so that it is greater or equal to a-head
        while(b && cmptemp(&b->tmp_temp, &a->tmp_temp) < 0) {
            b = b->tmp_list;
        }

        if (b && cmptemp(&a->tmp_temp, &b->tmp_temp) == 0) {
            // skip both
        } else {
            result = temp_list_cons(a->tmp_temp, result);
        }
    }
    return list_reverse(result);
}

static _Bool temp_list_eq(const temp_list_t* a, const temp_list_t* b)
{
    while (a || b) {
        if (!(a && b)) {
            return 0;
        }
        if (cmptemp(&a->tmp_temp, &b->tmp_temp) != 0) {
            return 0;
        }
        a = a->tmp_list;
        b = b->tmp_list;
    }
    return 1;
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

struct igraph_and_table intererence_graph(lv_flowgraph_t* flow)
{
    // compute live out
    struct live_set {
        temp_list_t* temp_list;
    };
    // live out
    Table_T live_in_map //  node -> struct live_set
        = Table_new(0, cmpnode, hashnode);
    Table_T live_out_map //  node -> struct live_set
        = Table_new(0, cmpnode, hashnode);

    var nodes = lv_nodes(flow->lvfg_control);
    for (var n = nodes; n; n = n->nl_list) {
        Table_put(live_in_map, n->nl_node, xmalloc(sizeof(struct live_set)));
        Table_put(live_out_map, n->nl_node, xmalloc(sizeof(struct live_set)));
    }

    // TODO: order nodes by depth-first search
    // this will work ok for a bit
    nodes = list_reverse(nodes);

    Table_T live_in_map_ = Table_new(0, cmpnode, hashnode);
    Table_T live_out_map_ = Table_new(0, cmpnode, hashnode);

    // calculate live-in and live-out sets iteratively
    for (;;) {
        for (var n = nodes; n; n = n->nl_list) {
            var node = n->nl_node;
            // copy the previous iteration
            // TODO: collect and free the old sets / reuse them a/b style
            // in'[n] = in[n]; out'[n] = out[n]
            freeif(Table_put(live_in_map_, node, Table_get(live_in_map, node)));
            struct live_set* out_ns = Table_get(live_out_map, node);
            freeif(Table_put(live_out_map_, node, out_ns));

            // in[n] = use[n] union (out[n] setminus def[n])
            temp_list_t* use_n = Table_get(flow->lvfg_use, node);
            temp_list_t* def_n = Table_get(flow->lvfg_def, node);

            var out_minus_def = temp_list_minus(out_ns->temp_list, def_n);
            var in_n = temp_list_union(use_n, out_minus_def);
            // TODO: free `out_minus_def` list structure

            // out[n] = union {in[s] for s in succ[n]}
            temp_list_t* out_n = NULL;
            lv_node_list_t* succ = lv_succ(node);
            for (var s = succ; s; s = s->nl_list) {
                struct live_set* in_ss = Table_get(live_in_map, s->nl_node);
                assert(in_ss);
                var in_s = in_ss->temp_list;
                out_n = temp_list_union(out_n, in_s);
            }

            // store results back into live_in_map and live_out_map
            out_ns = xmalloc(sizeof *out_ns);
            out_ns->temp_list = out_n;
            Table_put(live_out_map, node, out_ns);

            struct live_set* in_ns = xmalloc(sizeof *in_ns);
            in_ns->temp_list = in_n;
            Table_put(live_in_map, node, in_ns);
        }

        _Bool match = 1;
        for (var n = nodes; n; n = n->nl_list) {
            var node = n->nl_node;

            struct live_set* in_ns_ = Table_get(live_in_map_, node);
            struct live_set* in_ns = Table_get(live_in_map, node);
            if (!temp_list_eq(in_ns_->temp_list, in_ns->temp_list)) {
                match = 0;
                break;
            }

            struct live_set* out_ns_ = Table_get(live_out_map_, node);
            struct live_set* out_ns = Table_get(live_out_map, node);
            if (!temp_list_eq(out_ns_->temp_list, out_ns->temp_list)) {
                match = 0;
                break;
            }
        }
        if (match)
            break;
    }

    // we are done with live_in_map and the diff maps
    Table_map(live_in_map_, vfree, NULL);
    Table_free(&live_in_map_);
    Table_map(live_out_map_, vfree, NULL);
    Table_free(&live_out_map_);
    Table_map(live_in_map, vfree, NULL);
    Table_free(&live_in_map);

    // Now we have the live-out sets, we can compute the interference graph
    lv_igraph_t* igraph = xmalloc(sizeof *igraph);
    igraph->lvig_graph = lv_new_graph();
    igraph->lvig_tnode = Table_new(0, cmptemp, hashtemp);
    igraph->lvig_gtemp = Table_new(0, cmpnode, hashnode);
    igraph->lvig_moves = NULL;

    // ok, now we need a node in the graph for each temp?
    // and a way of associating them

    for (var n = nodes; n; n = n->nl_list) {
        var node = n->nl_node;
        temp_list_t* def_n = Table_get(flow->lvfg_def, node);
        struct live_set* out_ns = Table_get(live_out_map, node);

        for (var d = def_n; d; d = d->tmp_list) {
            lv_node_t* d_node = ig_get_node_for_temp(igraph, &d->tmp_temp);

            for (var t = out_ns->temp_list; t; t = t->tmp_list) {
                lv_node_t* t_node = ig_get_node_for_temp(igraph, &t->tmp_temp);

                lv_mk_edge(d_node, t_node);

                if (Table_get(flow->lvfg_ismove, node)) {
                    igraph->lvig_moves = lv_node_pair_cons(
                            lv_node_pair(d_node, t_node), igraph->lvig_moves);
                }
            }
        }
    }

    Table_T live_outs = Table_new(0, cmpnode, hashnode);

    // convert the live outs into a map just to temp_list
    for (var n = nodes; n; n = n->nl_list) {
        struct live_set* out_ns = Table_remove(live_out_map, n->nl_node);
        if (out_ns->temp_list) {
            Table_put(live_outs, n->nl_node, out_ns->temp_list);
        }
    }
    Table_map(live_out_map, vfree, NULL);
    Table_free(&live_out_map);

    // TODO: free `nodes`

    struct igraph_and_table result = {
        .igraph = igraph,
        .live_outs = live_outs,
    };
    return result;
}

void igraph_show(FILE* out, lv_igraph_t* igraph)
{

    var nodes = lv_nodes(igraph->lvig_graph);

    for (var n = nodes; n; n = n->nl_list) {
        // TODO
    }
}
