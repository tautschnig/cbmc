# Slot-pun family, dict-value member (PLR §3.2 Any-dominance): a CONTAINER
# first store into a pending empty dict ({} under a bare `object`/no
# annotation) infers python_value key/value types -- the scalar default
# punned the stored list into an int slot, so items() bound the value as
# int and iterating it false-alarmed the not-iterable obligation (the
# bedrock providers-by-key shape; CPython runs this cleanly). Also covers
# the METHOD scope: the function-body pre-scans (escaped mutables,
# empty-list/dict inference) now run for ClassDef methods too -- they were
# module-scope-only, so a method-local {} never got the inference.
from typing import Any, Dict, List


class M:
    def cat(self, models: List[Dict[str, Any]]) -> Dict[str, List[Dict[str, Any]]]:
        providers: object = {}
        for m in models:
            k: Any = m.get("p", "u")
            if k not in providers:
                providers[k] = []
            providers[k].append(m)
        return providers


m = M()
ps = m.cat([{"p": "a"}, {"p": "b"}])
for prov, pms in ps.items():
    for pm in pms:
        pass
assert True
