# del + __getattr__ through an ANNOTATED function parameter (o: F). The instance
# is by-reference through the param, and the annotated param type lets the Delete
# handler resolve the class and store __getattr__'s result, so the mutation
# propagates back and the cross-type use is caught. (An UNannotated/Any param
# would not resolve the class -- the structural-mutation-through-a-call / c2
# family.)
class F:
    def __init__(self) -> None:
        self.x: int = 42

    def __getattr__(self, name: str) -> str:
        return "fb"


def clr(o: F) -> None:
    del o.x


def main() -> None:
    f = F()
    clr(f)
    r = f.x - 1   # "fb" - 1 -> TypeError


main()
