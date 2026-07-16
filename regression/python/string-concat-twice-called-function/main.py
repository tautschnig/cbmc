# CORE [soundness]: a string CONCAT inside a function CALLED TWICE previously
# emitted its solver output symbols as GLOBALS (the binop-Add concat site did
# not pass the enclosing-function scope to emit_string_function), so the two
# executions asserted conflicting content constraints over one SSA value ->
# UNSAT -> EVERY property in the program vacuously SUCCESSFUL -- a GLOBAL false
# proof (found via the boto3 factory-raise stub-contract vacuity: 51-benchmark
# corpus programs were silently unverified). With the scope passed, the symbols
# are function-local and DECL'd per invocation. CPython: mk('beta') then
# mk('alpha') returns A; A.am()'s assert False fires -> VERIFICATION FAILED.
class A:
    def am(self):
        assert False


class B:
    def bm(self):
        return 2


def mk(name: str):
    if name == "alpha":
        return A()
    if name == "beta":
        return B()
    raise Exception("unknown: " + name)


x: B = mk('beta')
y: A = mk('alpha')
y.am()
