// vim:ft= :
struct X {
    x: int,
    y: int,
}
struct Y {
    x: X,
    y: X,
}

struct IntList {
    head: int,
    tail: *IntList,
}

fn f(x: int, y: int) -> int {
    let a: int = g(x, y);
    let b: *X = new X { 1, 2 };

    return a + b->x;
}

fn g(x: int, y: int) -> int {
    x * y
}

fn x_eq(w: Y, z: Y) -> bool {
    w == z
}

// to compare with x_eq
fn ex_eq(w: Y, z: Y) -> bool {
    w.x == z.x
    && w.y == z.y
}

fn main() -> int {
    return f(1, 100);
}
