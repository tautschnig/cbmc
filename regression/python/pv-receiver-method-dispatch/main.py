# pv-CLASS receiver method dispatch: a method call on an Any/python_value
# receiver dispatches through __class_ptr on __class_tag. The virtual-
# dispatch gate now admits BOTH tagged-union forms (struct_tag AND the
# expanded plain struct) -- only the former reached it, so e.g.
# `r.get(...)` on an Any-held stub response collapsed the whole call to
# nondet, severing provenance for everything downstream (the bedrock
# family). The result's provenance survives: Box.mk returns an empty
# list via its identity-dispatched call.
from typing import Any


class Box:
    def mk(self) -> list:
        return []


def get() -> Any:
    return Box()


r: Any = get()
xs: Any = r.mk()
for x in xs:
    pass
assert True
