# PLR §4.4 instance truthiness through the Any channel: __bool__ wins over
# __len__; a constant __len__ -> 0 makes the instance FALSY. The old model
# treated every CLASS-tagged python_value as truthy -- a FALSE PROOF (the
# `if response:` guard on a len-0 stub object executed a branch CPython
# never takes) and a path-explosion driver on the stub-heavy corpus. The
# classification is per-instance identity-dispatched (__class_tag);
# non-constant deciders stay nondet (sound both directions).
from typing import Any


class Empty:
    def __len__(self):
        return 0


class Full:
    def __len__(self):
        return 5


def get_e() -> Any:
    return Empty()


def get_f() -> Any:
    return Full()


e: Any = get_e()
if e:
    assert False  # CPython: never reached (len 0 -> falsy)
f: Any = get_f()
if f:
    pass
else:
    assert False  # CPython: never reached (len 5 -> truthy)
assert True
