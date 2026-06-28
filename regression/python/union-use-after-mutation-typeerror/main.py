# differential: a Union (int | str) attribute is read as int after being
# mutated to str within the same expression's left-to-right evaluation.
# CPython evaluates e.gm() first (sets e.x = "mut"), then e.x, giving
# 5 + "mut" -> TypeError. cbmc sees the mutation (reference semantics) but
# unwrap of the tagged-union field reads the int slot with no tag obligation,
# so no TypeError is raised. PLR-correct outcome is VERIFICATION FAILED.
class E:
    def __init__(self) -> None:
        self.x: "int | str" = 10

    def gm(self) -> int:
        self.x = "mut"
        return 5

e = E()
r = e.gm() + e.x
