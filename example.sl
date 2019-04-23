// vim:ft= :
struct X {
    x: int,
    y: int,
}

struct IntList {
    head: int,
    tail: *IntList,
}

fn f(x: int, y: int) -> int {
    let a: int = g(x, y);
    // let b: *X = new X { 1, 2 };

    return a;
}

fn main() -> int {
    return f(1, 100);
}
