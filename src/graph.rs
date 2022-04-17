use std::ptr::null;

type Node_ = usize;

#[repr(C)]
pub struct Node {
    graph: *mut Graph,
    idx: Node_,
}

#[repr(C)]
pub struct NodeList {
    nl_node: *mut Node,
    nl_list: *const NodeList,
}

#[derive(Debug)]
pub struct Graph {
    nodes: Vec<NodeRep>,
}

#[derive(Debug)]
pub struct NodeRep {
    succ: Vec<Node_>,
    pred: Vec<Node_>,
}

impl NodeRep {
    fn new() -> NodeRep {
        NodeRep {
            succ: Vec::new(),
            pred: Vec::new(),
        }
    }
}

#[no_mangle]
pub extern "C" fn lv_nodes(g: *mut Graph) -> *const NodeList {
    let mut result = null();

    let graph = unsafe { &*g };
    for j in 1..=graph.nodes.len() {
        let i = graph.nodes.len() - j;
        let new_node = Box::new(Node{graph: g, idx: i});
        let nodeptr = Box::into_raw(new_node);
        result = Box::into_raw(Box::new(NodeList {
            nl_node: nodeptr,
            nl_list: result,
        }));
    }
    return result;
}

#[no_mangle]
pub extern "C" fn lv_succ(n: *mut Node) -> *const NodeList {
    let mut result = null();

    let node = unsafe { &*n };
    let graph = unsafe { &*node.graph };

    for idx in graph.nodes[node.idx].succ.iter() {
        let new_node = Box::new(Node{ graph: node.graph, idx: *idx });
        let nodeptr = Box::into_raw(new_node);
        result = Box::into_raw(Box::new(NodeList {
            nl_node: nodeptr,
            nl_list: result,
        }));
    }
    return result;
}

#[no_mangle]
pub extern "C" fn lv_pred(n: *mut Node) -> *const NodeList {
    let mut result = null();

    let node = unsafe { &*n };
    let graph = unsafe { &*node.graph };

    for idx in graph.nodes[node.idx].pred.iter() {
        let new_node = Box::new(Node{ graph: node.graph, idx: *idx });
        let nodeptr = Box::into_raw(new_node);
        result = Box::into_raw(Box::new(NodeList {
            nl_node: nodeptr,
            nl_list: result,
        }));
    }
    return result;
}

// Returns all nodes with an edge from or to `n`
#[no_mangle]
pub extern "C" fn lv_adj(n: *mut Node) -> *const NodeList {
    let mut result = null();

    let node = unsafe { &*n };
    let graph = unsafe { &*node.graph };

    let mut adj: Vec<Node_> = Vec::new();

    for idx in graph.nodes[node.idx].pred.iter().chain(graph.nodes[node.idx].succ.iter()) {
        if !adj.contains(idx) {
            adj.push(*idx);
        }
    }

    adj.sort();

    for idx in adj.iter().rev() {
        let new_node = Box::new(Node{ graph: node.graph, idx: *idx });
        let nodeptr = Box::into_raw(new_node);
        result = Box::into_raw(Box::new(NodeList {
            nl_node: nodeptr,
            nl_list: result,
        }));
    }
    return result;
}

#[no_mangle]
pub extern "C" fn lv_eq(a: *const Node, b: *const Node) -> bool {
    let nodea = unsafe { &*a };
    let nodeb = unsafe { &*b };

    nodea.graph == nodeb.graph  && nodea.idx == nodeb.idx
}

#[no_mangle]
pub extern "C" fn lv_new_graph() -> *mut Graph {
    let g = Box::new(Graph {
        nodes: Vec::new(),
    });

    Box::into_raw(g)
}

#[no_mangle]
pub extern "C" fn lv_free_graph(g: *mut Graph) {
    unsafe { Box::from_raw(g) };
}


#[no_mangle]
pub extern "C" fn lv_new_node(g: *mut Graph) -> *mut Node  {
    let graph = unsafe { &mut *g };

    graph.nodes.push(NodeRep::new());
    let new_node = Box::new(Node{graph: g, idx: graph.nodes.len() - 1});
    Box::into_raw(new_node)
}

#[no_mangle]
pub extern "C" fn lv_mk_edge(pfrom: *mut Node, pto: *mut Node) {
    let from = unsafe { &*pfrom };
    let to = unsafe { &*pto };
    if from.graph != to.graph {
        panic!("from and to not from same graph");
    }
    let graph = unsafe { &mut *from.graph };
    if !graph.nodes[from.idx].succ.contains(&to.idx) {
        graph.nodes[from.idx].succ.push(to.idx);
        graph.nodes[to.idx].pred.push(from.idx);
    }
}

#[no_mangle]
pub extern "C" fn lv_rm_edge(pfrom: *mut Node, pto: *mut Node) {
    let from = unsafe { &*pfrom };
    let to = unsafe { &*pto };
    if from.graph != to.graph {
        panic!("from and to not from same graph");
    }
    let graph = unsafe { &mut *from.graph };
    // there's got to be a prettier way of doing this...
    // but I have no idea
    let mut i = 0;
    while i < graph.nodes[from.idx].succ.len() {
        if graph.nodes[from.idx].succ[i] == to.idx {
            graph.nodes[from.idx].succ.remove(i);
        } else {
            i += 1;
        }
    }
    i = 0;
    while i < graph.nodes[to.idx].pred.len() {
        if graph.nodes[to.idx].pred[i] == from.idx {
            graph.nodes[to.idx].pred.remove(i);
        } else {
            i += 1;
        }
    }
}

#[no_mangle]
pub extern "C" fn lv_print_graph(g: *mut Graph) {
    let graph = unsafe { &*g };
    println!("{:?}", graph);
}


