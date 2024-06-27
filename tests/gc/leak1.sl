// vim: ft= syntax=structlang
struct X {
    x: int,
    y: int,
}


fn makeX10(x: int, y: int) -> *X {
    let a: *X = new X { x, y };
    let b: *X = new X { x, y };
    let c: *X = new X { x, y };
    let d: *X = new X { x, y };
    let e: *X = new X { x, y };
    let f: *X = new X { x, y };
    let g: *X = new X { x, y };
    let h: *X = new X { x, y };
    let i: *X = new X { x, y };
    let j: *X = new X { x, y };
    a
}
fn makeX100(x: int, y: int) -> *X {
    let a: *X = makeX10(x, y);
    let b: *X = makeX10(x, y);
    let c: *X = makeX10(x, y);
    let d: *X = makeX10(x, y);
    let e: *X = makeX10(x, y);
    let f: *X = makeX10(x, y);
    let g: *X = makeX10(x, y);
    let h: *X = makeX10(x, y);
    let i: *X = makeX10(x, y);
    let j: *X = makeX10(x, y);
    j
}

fn main() -> int {
    let a: *X = makeX100(2, 1);
    return a->x;
}
