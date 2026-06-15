"""
Verification-optimized model of the `re` module (regular
expressions).

This is the shallow-stub layer described in
``doc/python-frontend-regex-story.md``: a shallow stub that
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

    def match(self, string: str, pos: int = 0, endpos: int = 0) -> "Match | None":
        # Return a real Match-or-None reflecting the SMT regex decision.
        # Under an SMT String solver (--cvc5 --python-smt-strings) the
        # __cbmc_re_match hook is precise, so Match()/None is exact; under the
        # default backend the hook is nondet, so the result is a nondet
        # Match-or-None and both `is not None` branches are explored (sound).
        if __cbmc_re_match(self.pattern, string):
            return Match()
        return None

    def fullmatch(self, string: str, pos: int = 0, endpos: int = 0) -> "Match | None":
        if __cbmc_re_fullmatch(self.pattern, string):
            return Match()
        return None

    def search(self, string: str, pos: int = 0, endpos: int = 0) -> "Match | None":
        if __cbmc_re_search(self.pattern, string):
            return Match()
        return None

    def findall(self, string, pos: int = 0, endpos: int = 0):
        # Sound over-approximation: a nondet (bounded) list of nondet strings.
        # Returning [] would be UNSOUND -- code that iterates the matches would
        # silently check nothing (missed bugs). Precise enumeration needs match
        # position extraction, which the SMT regex intrinsics do not provide.
        return nondet_list(8, nondet_str())

    def finditer(self, string, pos: int = 0, endpos: int = 0):
        # Sound over-approximation: a nondet (bounded) list of nondet Match
        # objects (finditer yields Match, not str).
        return nondet_list(8, Match())

    def split(self, string, maxsplit: int = 0):
        # Sound over-approximation: a nondet (bounded) list of nondet strings.
        return nondet_list(8, nondet_str())

    def sub(self, repl: str, string: str, count: int = 0) -> str:
        # Precise for the sound subset (constant fixed-length pattern,
        # literal repl) when replacing all (count == 0); a bounded count or
        # an out-of-subset pattern/repl yields a sound nondet string.
        return __cbmc_re_sub(self.pattern, repl, string, count)

    def subn(self, repl: str, string: str, count: int = 0):
        return (self.sub(repl, string, count), 0)


# Module-level functions. Each returns a value of the shape CPython
# returns: a Match (or None), a list, a tuple, or a string.


def compile(pattern, flags: int = 0) -> Pattern:
    return Pattern(pattern, flags)


def match(pattern: str, string: str, flags: int = 0) -> "Match | None":
    # Real Match-or-None from the SMT regex decision; see Pattern.match.
    # Compilation flags (IGNORECASE/MULTILINE/DOTALL/...) change the match
    # semantics and are not modelled by the SMT regex translation, so a
    # non-zero flag must NOT commit to the flag-free (e.g. case-sensitive)
    # decision -- that would be unsound. Fall back to a nondet Match-or-None.
    if flags != 0:
        return Match() if nondet_bool() else None
    if __cbmc_re_match(pattern, string):
        return Match()
    return None


def fullmatch(pattern: str, string: str, flags: int = 0) -> "Match | None":
    if flags != 0:
        return Match() if nondet_bool() else None
    if __cbmc_re_fullmatch(pattern, string):
        return Match()
    return None


def search(pattern: str, string: str, flags: int = 0) -> "Match | None":
    if flags != 0:
        return Match() if nondet_bool() else None
    if __cbmc_re_search(pattern, string):
        return Match()
    return None


def findall(pattern, string, flags: int = 0):
    # Sound over-approximation: a nondet (bounded) list of nondet strings.
    return nondet_list(8, nondet_str())


def finditer(pattern, string, flags: int = 0):
    # Sound over-approximation: a nondet (bounded) list of nondet Match objects.
    return nondet_list(8, Match())


def split(pattern, string, maxsplit: int = 0, flags: int = 0):
    # Sound over-approximation: a nondet (bounded) list of nondet strings.
    return nondet_list(8, nondet_str())


def sub(pattern: str, repl: str, string: str, count: int = 0, flags: int = 0) -> str:
    return __cbmc_re_sub(pattern, repl, string, count)


def subn(pattern: str, repl: str, string: str, count: int = 0, flags: int = 0):
    return (sub(pattern, repl, string, count, flags), 0)


def escape(pattern: str) -> str:
    return ""


def purge() -> None:
    return None
