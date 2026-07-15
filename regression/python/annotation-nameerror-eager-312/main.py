# Version-dependent annotation semantics (PEP 649): under Python <= 3.13 (the
# suite's parsing interpreter) annotations are EAGERLY evaluated, so an
# unimported annotation name (`-> Any` without `from typing import Any`) raises
# NameError when the def executes -- before f is ever called. Under a 3.14+
# parser the frontend switches to LAZY semantics (future_annotations), and this
# program verifies (guarded by a spoofed-version AST-server test during
# development; this CORE pin covers the eager side while the suite runs <=3.13).
def f() -> Any:
    return 1


f()
