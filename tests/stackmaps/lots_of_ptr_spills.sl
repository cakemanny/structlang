// vim:ft= :

struct X {
    x: int,
    y: int,
}
fn diag(x: int) -> *X { new X { x, x } }

fn plus2(a: *X, b: *X) -> *X {
    new X { a->x + b->x , a->y + b->y }
}
fn plus3(a: *X, b: *X, c: *X) -> *X {
    new X { a->x + b->x + c->x, a->y + b->y + c->y }
}
fn plus4(a: *X, b: *X, c: *X, d: *X) -> *X {
     new X { a->x + b->x + c->x + d->x, a->y + b->y + c->y +d->y }
}

fn main() -> int {
    let z: X = *plus4(
        plus2(plus2(diag(1), diag(2)) , plus2(diag(3),diag(4))),
        plus2(plus3(diag(5),diag(6),diag(7)) , plus3(diag(8),diag(9),diag(10))),
        plus2(plus3(diag(5),diag(6),diag(7)) , plus3(diag(8),diag(9),diag(10))),
        plus4(plus2(diag(1), diag(2)) , plus2(diag(3),diag(4)) , plus2(diag(1), diag(2)) , plus2(diag(3),diag(4)))
    );
    z.x + z.y
}

