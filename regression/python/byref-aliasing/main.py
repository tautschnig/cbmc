# PLR §3.1: Mutable containers (list, dict) are passed by reference.
# Stage 3 of the by-reference refactor extends this from function-
# parameter binding (Stage 1) to assignment binding: `b = a` where
# `a` is a list/dict variable promotes `b` to a pointer-to-storage
# alias of `a`, so mutations through `b` are visible through `a`.

def list_alias() -> None:
    a: list = [1, 2, 3]
    b: list = a
    b[0] = 99
    # Aliased — Python observes 99 here.
    assert a[0] == 99
    assert b[0] == 99


def dict_alias() -> None:
    a: dict = {"k": 1}
    b: dict = a
    b["k"] = 99
    assert a["k"] == 99


def transitive_alias() -> None:
    # c = b = a chain. The converter records the FINAL target so c
    # aliases a directly, not "an alias of an alias".
    a: dict = {"k": 1}
    b: dict = a
    c: dict = b
    c["k"] = 99
    assert a["k"] == 99
    assert b["k"] == 99
    assert c["k"] == 99


def copy_is_not_alias() -> None:
    # `.copy()` MUST produce a fresh container, not an alias. This
    # is the crucial counter-example: the importer-side AST shape
    # gating prevents .copy()'s symbol-returning handler from
    # inadvertently triggering the alias-promotion path.
    a: list = [1, 2, 3]
    b: list = a.copy()
    b[0] = 99
    assert a[0] == 1   # copy is independent
    assert b[0] == 99


list_alias()
dict_alias()
transitive_alias()
copy_is_not_alias()
