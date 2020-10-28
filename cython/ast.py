from dataclasses import dataclass
from typing import List


class Type:
    pass


class Decl:
    pass


@dataclass
class Name(Type):
    name: str


@dataclass
class Pointer(Type):
    pointee: Type


@dataclass
class FuncT(Type):
    pass


@dataclass
class Param(Decl):
    name: str
    type: Type
    line: int


@dataclass
class Struct(Decl):
    name: str
    params: List[Param]
    line: int


@dataclass
class Func(Decl):
    name: str
    params: List[Param]
    type: Type
    body: None
    line: int
