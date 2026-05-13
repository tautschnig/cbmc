# When an import fails to resolve (module not found), the
# imported names shouldn't trigger 'no body for callee'
# properties — the tool can't be sound about their behaviour
# but the user's source isn't wrong.

from custom_unknown_module import do_thing, helper


def main() -> int:
    r = do_thing(42)
    h = helper()
    return r + h


x = main()
# Nondet result — no assertion, but reaching here means
# the no-body checks didn't fire spuriously on the
# unresolved imports.
