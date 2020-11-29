type Node_ = usize;
type Node = (*mut Graph, Node_);

#[derive(Debug)]
pub struct Graph {
    nodes: Vec<NodeRep>,
    deleted: Vec<Node_>,
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
pub extern "C" fn lv_eq(a: *const Node, b: *const Node) -> bool {
    let (ga, a_idx) = unsafe { *a };
    let (gb, b_idx) = unsafe { *b };

    ga == gb && a_idx == b_idx
}

#[no_mangle]
pub extern "C" fn lv_new_graph() -> *mut Graph {
    let g = Box::new(Graph {
        nodes: Vec::new(),
        deleted: Vec::new(),
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
    if let Some(idx) = graph.deleted.pop() {
        let new_node = Box::new((g, idx));
        Box::into_raw(new_node)
    } else {
        graph.nodes.push(NodeRep::new());
        let new_node = Box::new((g, graph.nodes.len() - 1));
        Box::into_raw(new_node)
    }
}

#[no_mangle]
pub extern "C" fn lv_mk_edge(pfrom: *mut Node, pto: *mut Node) {
    let (g, from_idx) = unsafe { *pfrom };
    let (g2, to_idx) = unsafe { *pto };
    if g != g2 {
        panic!("from and to not from same graph");
    }
    let graph = unsafe { &mut *g };
    graph.nodes[from_idx].succ.push(to_idx);
    graph.nodes[to_idx].pred.push(from_idx);
}

#[no_mangle]
pub extern "C" fn lv_rm_edge(pfrom: *mut Node, pto: *mut Node) {
    let (g, from_idx) = unsafe { *pfrom };
    let (g2, to_idx) = unsafe { *pto };
    if g != g2 {
        panic!("from and to not from same graph");
    }
    let graph = unsafe { &mut *g };
    // there's got to be a prettier way of doing this...
    // but I have no idea
    let mut i = 0;
    while i < graph.nodes[from_idx].succ.len() {
        if graph.nodes[from_idx].succ[i] == to_idx {
            graph.nodes[from_idx].succ.remove(i);
        } else {
            i += 1;
        }
    }
    i = 0;
    while i < graph.nodes[to_idx].pred.len() {
        if graph.nodes[to_idx].pred[i] == from_idx {
            graph.nodes[to_idx].pred.remove(i);
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


