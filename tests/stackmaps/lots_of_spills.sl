
fn plus2(a: int, b: int) -> int { a + b }
fn plus3(a: int, b: int, c: int) -> int { a + b + c }
fn plus4(a: int, b: int, c: int, d: int) -> int { a + b + c + d }

fn main() -> int {
    plus4(
        plus2(1, 2) + plus2(3,4),
        plus3(5,6,7) + plus3(8,9,10),
        plus3(5,6,7) + plus3(8,9,10),
        plus2(1, 2) + plus2(3,4) + plus2(1, 2) + plus2(3,4)
    )
}
