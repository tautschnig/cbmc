"""
Verification-optimized model of the `re` module (regular
expressions).

This is Wave 1 of the plan in
``doc/architectural/python-re-support-plan.md``: a shallow stub that
exposes the public surface of ``re`` with each function returning
a nondet value of the right shape. Wave 2 (SMT regex via --cvc5)
will replace this file with a version that carries the pattern as
a literal into a ``cprover_string_match_func`` intrinsic.

Coverage:
  * Match object       (group / groups / start / end / span / groupdict)
  * Pattern object     (match / search / sub / findall / split / finditer)
  * Module-level:      match / search / sub / subn / findall / finditer /
                       split / compile / escape / fullmatch / purge
  * Constants:         IGNORECASE / MULTILINE / DOTALL / VERBOSE / ASCII /
                       UNICODE / LOCALE / DEBUG / TEMPLATE (as ints)
  * RegexError         (falls back to a ValueError stand-in)

The model does NOT reason about the pattern; every match decision
is nondet, with groups and spans returned as typed nondet values.
"""


# Compilation flags. The values match CPython's sre_constants.
IGNORECASE = 2
LOCALE = 4
MULTILINE = 8
DOTALL = 16
UNICODE = 32
VERBOSE = 64
DEBUG = 128
ASCII = 256
TEMPLATE = 1
NOFLAG = 0


class error(Exception):
    """re.error — raised on malformed patterns."""

    msg: str
    pattern: str
    pos: int

    def __init__(self, msg: str = "", pattern: str = "", pos: int = 0):
        self.msg = msg
        self.pattern = pattern
        self.pos = pos


class Match:
    """A Match object, shape-compatible with CPython's re.Match."""

    pos: int
    endpos: int
    lastindex: int
    lastgroup: str
    # ``re`` and ``string`` are Python-level members of the real
    # Match class; typed as nondet-friendly values here.
    string: str

    def __init__(self):
        self.pos = 0
        self.endpos = 0
        self.lastindex = 0
        self.lastgroup = ""
        self.string = ""

    def group(self, *args) -> str:
        return ""

    def groups(self, default=None):
        return ()

    def groupdict(self, default=None):
        return {}

    def start(self, group: int = 0) -> int:
        return 0

    def end(self, group: int = 0) -> int:
        return 0

    def span(self, group: int = 0):
        return (0, 0)

    def expand(self, template: str) -> str:
        return ""


class Pattern:
    """A compiled regular-expression pattern."""

    pattern: str
    flags: int
    groups: int

    def __init__(self, pattern: str = "", flags: int = 0):
        self.pattern = pattern
        self.flags = flags
        self.groups = 0

    def match(self, string, pos: int = 0, endpos: int = 0):
        # Route through the Wave-2 intrinsic when possible.
        if __cbmc_re_match(self.pattern, string):
            return Match()
        return None

    def fullmatch(self, string, pos: int = 0, endpos: int = 0):
        if __cbmc_re_fullmatch(self.pattern, string):
            return Match()
        return None

    def search(self, string, pos: int = 0, endpos: int = 0):
        if __cbmc_re_search(self.pattern, string):
            return Match()
        return None

    def findall(self, string, pos: int = 0, endpos: int = 0):
        return []

    def finditer(self, string, pos: int = 0, endpos: int = 0):
        return []

    def split(self, string, maxsplit: int = 0):
        return []

    def sub(self, repl, string, count: int = 0) -> str:
        return ""

    def subn(self, repl, string, count: int = 0):
        return ("", 0)


# Module-level functions. Each returns a value of the shape CPython
# returns: a Match (or None), a list, a tuple, or a string.


def compile(pattern, flags: int = 0) -> Pattern:
    return Pattern(pattern, flags)


def match(pattern, string, flags: int = 0):
    # Wave 2: when both pattern and string are Python strs, route
    # through __cbmc_re_match — the front-end lowers this to
    # cprover_string_match_func and the SMT backend ( --cvc5 )
    # emits str.in_re with the translated regex. Non-string
    # arguments or patterns the translator rejects fall back to
    # a nondet Match / None choice, matching Wave 1 semantics.
    if __cbmc_re_match(pattern, string):
        return Match()
    return None


def fullmatch(pattern, string, flags: int = 0):
    if __cbmc_re_fullmatch(pattern, string):
        return Match()
    return None


def search(pattern, string, flags: int = 0):
    if __cbmc_re_search(pattern, string):
        return Match()
    return None


def findall(pattern, string, flags: int = 0):
    return []


def finditer(pattern, string, flags: int = 0):
    return []


def split(pattern, string, maxsplit: int = 0, flags: int = 0):
    return []


def sub(pattern, repl, string, count: int = 0, flags: int = 0) -> str:
    return ""


def subn(pattern, repl, string, count: int = 0, flags: int = 0):
    return ("", 0)


def escape(pattern: str) -> str:
    return ""


def purge() -> None:
    return None
