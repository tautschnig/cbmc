# PLR §4.2.2: "When a name is not found at all, a NameError exception
# is raised." Three shapes that previously resolved silently to nondet:
#  - a bare undefined name inside a function (the main-module-defs
#    gate was populated only after function bodies were converted);
#  - a name whose only binding is a comprehension target used BEFORE
#    the comprehension (PLR §6.2.4: comprehension targets live in a
#    separate implicitly nested scope, invisible outside);
#  - a name defined only in an imported module's stub but never
#    imported (PLR §7.11: `from datetime import datetime` binds ONLY
#    `datetime`; reading `timezone` is a NameError).
from datetime import datetime


def bare():
    return undefined_thing + 1


def before_comp():
    xs = ["a", "b"]
    total = str(r) + "x"
    d = {r: 1 for r in xs}
    return total


def leaked_module_name():
    return int(datetime.now(timezone.utc).timestamp())


bare()
before_comp()
leaked_module_name()
