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


def _re_flag_prefix(flags: int) -> str:
    # Map the `flags=` bitmask to a regex inline-flag prefix (e.g. re.IGNORECASE
    # -> "(?i)"), so the SMT/constant-fold regex engine receives the flags IN
    # the pattern (no separate flags channel). IGNORECASE and DOTALL are
    # modelled; any other flag bit (MULTILINE/VERBOSE/ASCII/...) is NOT, so we
    # return the sentinel "\x00" telling the caller to fall back to a sound
    # nondet Match-or-None rather than commit to a flag-free decision.
    # (Defined before the Match/Pattern classes so their methods resolve it.)
    if flags == 0:
        return ""
    supported = IGNORECASE | DOTALL
    if flags & ~supported:
        return "\x00"
    letters = ""
    if flags & IGNORECASE:
        letters = letters + "i"
    if flags & DOTALL:
        letters = letters + "s"
    return "(?" + letters + ")"


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
        # Match span + matched text carried from the position intrinsics (set
        # by search()/fullmatch()). The group-0 text is sliced from the LOCAL
        # subject at construction time and stored here, because slicing a
        # string held in an instance attribute is imprecise (see the
        # instance-__dict__ substrate gap); reading a stored string back is
        # precise. Defaults give the previous flat behaviour.
        self._start = 0
        self._end = 0
        self._group0 = ""
        # Capture-group slots (1..4), filled at match time by the
        # __cbmc_re_group decomposition intrinsic (see group()). Groups beyond
        # the 4th read back as a sound nondet string. A default of "" is only
        # seen by a bare Match() that no match path produced.
        self._group1 = ""
        self._group2 = ""
        self._group3 = ""
        self._group4 = ""

    def group(self, n: int = 0) -> str:
        # group(0) / no-arg: the whole match. Precise when the position
        # intrinsics resolved (fixed-length pattern, constant subject), or for
        # fullmatch (the whole subject); a nondet span yields a sound nondet
        # string. Sub-groups (n>=1) are extracted at match time by the
        # __cbmc_re_group decomposition (precise for literal-pinned groups,
        # sound nondet otherwise); groups beyond the 4 tracked slots are a
        # sound nondet string (never a concrete wrong value).
        if n == 0:
            return self._group0
        if n == 1:
            return self._group1
        if n == 2:
            return self._group2
        if n == 3:
            return self._group3
        if n == 4:
            return self._group4
        return nondet_str()

    def groups(self, default=None):
        # The captured groups depend on the (symbolic) match; a fixed () was a
        # false proof (it made `m.groups() == ()` verify even when the pattern
        # has groups). Sound nondet: a bounded list of nondet strings.
        return nondet_list(8, nondet_str())

    def groupdict(self, default=None) -> dict:
        # Likewise a fixed {} was a false proof for `m.groupdict() == {}`.
        # The `-> dict` annotation is required so the returned value is typed as
        # a dict at the call site (without it the dict-equality path is skipped
        # and `== {}` wrongly verifies -- a function-return type-propagation
        # gap; lists do not need this).
        return nondet_dict(8)

    def start(self, group: int = 0) -> int:
        return self._start

    def end(self, group: int = 0) -> int:
        return self._end

    def span(self, group: int = 0):
        return (self._start, self._end)

    def expand(self, template: str) -> str:
        # Sound nondet: the expanded result depends on the (symbolic) matched
        # groups; a fixed "" was a false proof (it made `m.expand(t) == ""`
        # verify even when CPython produces a non-empty string).
        return nondet_str()


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
        pat = self.pattern
        if self.flags != 0:
            pfx = _re_flag_prefix(self.flags)
            if pfx == "\x00":
                return Match() if nondet_bool() else None
            pat = pfx + self.pattern
        if __cbmc_re_match(pat, string):
            return Match()
        return None

    def fullmatch(self, string: str, pos: int = 0, endpos: int = 0) -> "Match | None":
        pat = self.pattern
        if self.flags != 0:
            pfx = _re_flag_prefix(self.flags)
            if pfx == "\x00":
                return Match() if nondet_bool() else None
            pat = pfx + self.pattern
        if __cbmc_re_fullmatch(pat, string):
            m = Match()
            m.string = string
            m._start = 0
            m._end = len(string)
            m._group0 = string
            return m
        return None

    def search(self, string: str, pos: int = 0, endpos: int = 0) -> "Match | None":
        pat = self.pattern
        if self.flags != 0:
            pfx = _re_flag_prefix(self.flags)
            if pfx == "\x00":
                return Match() if nondet_bool() else None
            pat = pfx + self.pattern
        if __cbmc_re_search(pat, string):
            m = Match()
            m.string = string
            st = __cbmc_re_search_start(pat, string, 0)
            en = __cbmc_re_search_end(pat, string, 0)
            m._start = st
            m._end = en
            m._group0 = string[st:en]
            return m
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
        # The substitution count is unknown; a fixed 0 was unsound (it made
        # `subn(...)[1] == 0` verify even when substitutions occur). Sound
        # nondet count.
        return (self.sub(repl, string, count), nondet_int())


# Module-level functions. Each returns a value of the shape CPython
# returns: a Match (or None), a list, a tuple, or a string.


def compile(pattern, flags: int = 0) -> Pattern:
    return Pattern(pattern, flags)


def match(pattern: str, string: str, flags: int = 0) -> "Match | None":
    # Real Match-or-None from the SMT regex decision; see Pattern.match.
    # Compilation flags are carried into the pattern as an inline-flag prefix
    # (re.IGNORECASE/DOTALL -> "(?i)"/"(?s)"); an unmodelled flag falls back to
    # a sound nondet Match-or-None rather than a flag-free decision. The prefix
    # is only prepended when flags are present, so the common flag-free case
    # keeps a constant (foldable) pattern.
    if flags != 0:
        pfx = _re_flag_prefix(flags)
        if pfx == "\x00":
            return Match() if nondet_bool() else None
        pattern = pfx + pattern
    if __cbmc_re_match(pattern, string):
        m = Match()
        m.string = string
        m._start = 0
        # Match is start-anchored; the match end is the leftmost match's end
        # (precise for a fixed-length pattern, sound nondet otherwise).
        en = __cbmc_re_search_end(pattern, string, 0)
        m._end = en
        g0 = string[0:en]
        m._group0 = g0
        m._group1 = __cbmc_re_group(pattern, g0, 1)
        m._group2 = __cbmc_re_group(pattern, g0, 2)
        m._group3 = __cbmc_re_group(pattern, g0, 3)
        m._group4 = __cbmc_re_group(pattern, g0, 4)
        return m
    return None


def fullmatch(pattern: str, string: str, flags: int = 0) -> "Match | None":
    if flags != 0:
        pfx = _re_flag_prefix(flags)
        if pfx == "\x00":
            return Match() if nondet_bool() else None
        pattern = pfx + pattern
    if __cbmc_re_fullmatch(pattern, string):
        m = Match()
        m.string = string
        m._start = 0
        m._end = len(string)
        m._group0 = string
        # Whole subject is the match text -> precise group decomposition when
        # the pattern is a constant, segmentable, literal-pinned capture.
        m._group1 = __cbmc_re_group(pattern, string, 1)
        m._group2 = __cbmc_re_group(pattern, string, 2)
        m._group3 = __cbmc_re_group(pattern, string, 3)
        m._group4 = __cbmc_re_group(pattern, string, 4)
        return m
    return None


def search(pattern: str, string: str, flags: int = 0) -> "Match | None":
    if flags != 0:
        pfx = _re_flag_prefix(flags)
        if pfx == "\x00":
            return Match() if nondet_bool() else None
        pattern = pfx + pattern
    if __cbmc_re_search(pattern, string):
        m = Match()
        m.string = string
        st = __cbmc_re_search_start(pattern, string, 0)
        en = __cbmc_re_search_end(pattern, string, 0)
        m._start = st
        m._end = en
        g0 = string[st:en]
        m._group0 = g0
        m._group1 = __cbmc_re_group(pattern, g0, 1)
        m._group2 = __cbmc_re_group(pattern, g0, 2)
        m._group3 = __cbmc_re_group(pattern, g0, 3)
        m._group4 = __cbmc_re_group(pattern, g0, 4)
        return m
    return None


def findall(pattern: str, string: str, flags: int = 0):
    if flags != 0:
        # Flags change match semantics and are not modelled here.
        return nondet_list(8, nondet_str())
    # Enumerate non-overlapping matches left to right via the match-position
    # intrinsics. Precise for a fixed-length pattern on a constant subject (the
    # positions fold; `result` is annotated list[str] so the element type is
    # pinned and the `pattern: str` / `string: str` annotations let the subject
    # fold into the intrinsics); an unsupported pattern / symbolic subject makes
    # the intrinsics nondet and this degrades to a sound bounded enumeration.
    # Bounded by the unwind limit (BMC).
    result: list[str] = []
    pos = 0
    n = len(string)
    while pos <= n:
        st = __cbmc_re_search_start(pattern, string, pos)
        if st < 0:
            break
        en = __cbmc_re_search_end(pattern, string, pos)
        result.append(string[st:en])
        # Advance past the match; +1 on an empty match to make progress.
        if en > pos:
            pos = en
        else:
            pos = pos + 1
    return result


def finditer(pattern, string, flags: int = 0):
    # Sound over-approximation: a nondet (bounded) list of nondet Match objects.
    return nondet_list(8, Match())


def split(pattern: str, string: str, maxsplit: int = 0, flags: int = 0):
    if flags != 0:
        return nondet_list(8, nondet_str())
    # Split on the non-overlapping matches of `pattern`: the gaps between
    # matches plus the trailing piece. `maxsplit > 0` caps the number of
    # splits. Precise for a fixed-length pattern on a constant subject;
    # otherwise a sound bounded enumeration.
    result: list[str] = []
    pos = 0
    last = 0
    splits = 0
    n = len(string)
    while pos <= n:
        if maxsplit > 0 and splits >= maxsplit:
            break
        st = __cbmc_re_search_start(pattern, string, pos)
        if st < 0:
            break
        en = __cbmc_re_search_end(pattern, string, pos)
        result.append(string[last:st])
        last = en
        splits = splits + 1
        if en > pos:
            pos = en
        else:
            pos = pos + 1
    result.append(string[last:n])
    return result

def sub(pattern: str, repl: str, string: str, count: int = 0, flags: int = 0) -> str:
    return __cbmc_re_sub(pattern, repl, string, count)


def subn(pattern: str, repl: str, string: str, count: int = 0, flags: int = 0):
    return (sub(pattern, repl, string, count, flags), nondet_int())


def escape(pattern: str) -> str:
    return ""


def purge() -> None:
    return None
