# strtab-aware constant recovery (smt2_conv::try_extract_string_literal /
# try_recover_strtab): a constant pattern stored through a HANDLE-typed
# class field (nondet handle + ghost-assignment strtab axiom, chased
# through SSA symbol definitions) is still recovered at conversion time
# by the regex lowering. This is what let the frontend-library handle
# exemption be lifted: re.Pattern.pattern itself is a handle field now.
import re

p = re.compile("ab+c")
assert p.match("abbc") is not None
assert p.match("ac") is None


class Rule:
    name: str

    def __init__(self, name: str) -> None:
        self.name = name


r = Rule("abc")
assert re.fullmatch("abc", r.name) is not None
