struct X { a: bool, b: bool, c: bool, d: bool, }

fn f(x: X) -> int {
    if (x.a && x.c) {
        5
    } else {
        10
    }
}

fn main() -> int {
    let x: *X = new X { false, true, false, true };
    f(*x)
}
