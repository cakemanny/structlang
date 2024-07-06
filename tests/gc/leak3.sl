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
    a
}

fn main() -> int {
    loop {
        makeX10(3, 4)
    }
}
