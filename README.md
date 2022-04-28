
Structlang is a simple language whose main feature is heap allocated composite
data structures. It's purpose is to allow me to play around with
garbage-collector implementations.

The syntax is strongly inspired by Rust and in places by C++.

Beyond the the parser and semantic analysis stages, the design follows
[Modern Compiler Implementation in ML][modernml]

[modernml]: https://www.cs.princeton.edu/~appel/modern/ml/

For simplicity, in many places memory is leaked unashamedly.
Given the one-shot nature of this compiler, that's not a problem.


```
struct X {
    x: int,
    y: int,
}
struct Y {
    x: X,
    y: X,
}
struct Z {
    x: int,
    y: int,
    z: int,
    w: int,
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

fn makeZ(x: int, y: int) -> Z {
    let b: *Z = new Z { 1, 2, 3, 4 };
    *b
}

fn main() -> int {
    return f(1, 100);
}
```

