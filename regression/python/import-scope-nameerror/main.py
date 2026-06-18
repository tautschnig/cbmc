# PLR 4.2: `from m import a` brings only `a` into scope. Referencing a
# name defined in m but NOT imported is a NameError. Previously the
# flat symbol table let `helper()` resolve to the leaked symbol.
from helper_mod import used


def run() -> None:
    used()
    not_imported()  # NameError: only `used` was imported


run()
