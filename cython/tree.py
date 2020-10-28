from dataclasses import dataclass
from enum import IntEnum
from typing import List


class Binop(IntEnum):
    PLUS = 1
    MINUS = 2
    MUL = 3
    DIV = 4
    AND = 5
    OR = 6
    XOR = 7
    LSHIFT = 8
    RSHIFT = 9
    ARSHIFT = 10


class Relop(IntEnum):
    EQ = 1
    NE = 2
    LT = 3
    GT = 4
    LE = 5
    GE = 6
    ULT = 7
    ULE = 8
    UGT = 9
    UGE = 10


class Exp:
    pass


class Stm:
    pass


@dataclass
class ExpConst(Exp):
    const: int


@dataclass
class ExpName(Exp):
    name: str


@dataclass
class ExpTemp(Exp):
    temp: int


@dataclass
class ExpBinop(Exp):
    binop: Binop
    lhs: Exp
    rhs: Exp


@dataclass
class ExpMem(Exp):
    addr: Exp
    size: int


@dataclass
class ExpCall(Exp):
    func: Exp
    args: List[Exp]


@dataclass
class ExpESeq(Exp):
    stm: Stm
    exp: Exp


@dataclass
class StmMove(Stm):
    dst: Exp
    exp: Exp


@dataclass
class StmExp(Stm):
    exp: Exp


@dataclass
class StmJump(Stm):
    dst: Exp
    num_labels: int
    labels: List[str]


@dataclass
class StmCJump(Stm):
    op: Relop
    lhs: Exp
    rhs: Exp
    true: str  # label
    false: str  # label


@dataclass
class StmLabel(Stm):
    label: str
