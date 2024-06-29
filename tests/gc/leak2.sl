// vim: ft= syntax=structlang

struct X {
    x: int,
    y: int,
}
struct P1 {
    x0: *X,
}
struct P2 {
    x0: *X,
    y0: int,
    y1: int,
    x1: *X,
}

fn p1(px: *X) -> *P1 {
    let r: *P1 = new P1 { px };
    r
}

fn p2(px: *P1) -> *P2 {
    let r: *P2 = new P2 {
        px->x0,
        px->x0->x,
        px->x0->y,
        px->x0  // have multiple pointers to same struct
    };
    r
}

fn makeP10(x: int, y: int) -> *P2 {
    let a: *P2 = p2(p1(new X { x, y }));
    let b: *P2 = p2(p1(new X { x, y }));
    let c: *P2 = p2(p1(new X { x, y }));
    let d: *P2 = p2(p1(new X { x, y }));
    let e: *P2 = p2(p1(new X { x, y }));
    let f: *P2 = p2(p1(new X { x, y }));
    let g: *P2 = p2(p1(new X { x, y }));
    let h: *P2 = p2(p1(new X { x, y }));
    let i: *P2 = p2(p1(new X { x, y }));
    let j: *P2 = p2(p1(new X { x, y }));
    a
}
fn makeP100(x: int, y: int) -> *P2 {
    let a: *P2 = makeP10(x, y);
    let b: *P2 = makeP10(x, y);
    let c: *P2 = makeP10(x, y);
    let d: *P2 = makeP10(x, y);
    let e: *P2 = makeP10(x, y);
    let f: *P2 = makeP10(x, y);
    let g: *P2 = makeP10(x, y);
    let h: *P2 = makeP10(x, y);
    let i: *P2 = makeP10(x, y);
    let j: *P2 = makeP10(x, y);
    j
}

fn main() -> int {
    let a: *P2 = makeP100(2, 1);
    return a->y0;
}
