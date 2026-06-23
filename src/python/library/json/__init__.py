"""
Verification-optimized model of the `json` module.

Exposes loads, dumps, load, dump as nondet-returning stubs. The
CPython implementation is substantial and performance-sensitive;
for verification we approximate the round-trip as nondet-on-parse.

Soundness: parse/serialize results are value-dependent, so they are
modelled as sound *nondet* values of the right shape -- a fixed concrete
default ("" / {} / None / []) would false-prove e.g. `json.loads(s) == {}`
or `json.dumps(x) == ""` (CPython returns non-empty for non-trivial input).
"""


class JSONDecodeError(ValueError):
    def __init__(self, msg: str = "", doc: str = "", pos: int = 0):
        self.msg = msg
        self.doc = doc
        self.pos = pos


class JSONEncoder:
    def __init__(self, *, skipkeys: bool = False, ensure_ascii: bool = True,
                 check_circular: bool = True, allow_nan: bool = True,
                 sort_keys: bool = False, indent=None, separators=None,
                 default=None):
        return None

    def default(self, o):
        return None

    def encode(self, o) -> str:
        # Value-dependent JSON string -> sound nondet (was "" : false proof).
        return nondet_str()

    def iterencode(self, o, _one_shot: bool = False):
        # Nondet bounded list of JSON-fragment strings (was [] : false proof).
        return nondet_list(8, nondet_str())


class JSONDecoder:
    def __init__(self, *, object_hook=None, parse_float=None,
                 parse_int=None, parse_constant=None, strict: bool = True,
                 object_pairs_hook=None):
        return None

    def decode(self, s: str, _w=None) -> dict:
        # Parsed JSON value (caller typically treats it as a dict) -> sound
        # nondet dict (was None : false proof for `decode(s) is None`).
        return nondet_dict(8)

    def raw_decode(self, s: str, idx: int = 0):
        # (value, end-index); both value-dependent -> sound nondet
        # (was (None, 0) : false proof for `raw_decode(s)[1] == 0`).
        return (nondet_dict(8), nondet_int())


def dump(obj, fp, *, skipkeys: bool = False, ensure_ascii: bool = True,
         check_circular: bool = True, allow_nan: bool = True,
         cls=None, indent=None, separators=None, default=None,
         sort_keys: bool = False, **kw) -> None:
    return None


def dumps(obj, *, skipkeys: bool = False, ensure_ascii: bool = True,
          check_circular: bool = True, allow_nan: bool = True, cls=None,
          indent=None, separators=None, default=None,
          sort_keys: bool = False, **kw) -> str:
    # Value-dependent JSON string -> sound nondet (was "" : false proof).
    return nondet_str()


def load(fp, *, cls=None, object_hook=None, parse_float=None,
         parse_int=None, parse_constant=None, object_pairs_hook=None,
         **kw) -> dict:
    # Nondet JSON value; caller typically treats the result as a dict or
    # list. Sound nondet dict (was {} : false proof for `load(...) == {}`).
    return nondet_dict(8)


def loads(s, *, cls=None, object_hook=None, parse_float=None,
          parse_int=None, parse_constant=None, object_pairs_hook=None,
          **kw) -> dict:
    # Sound nondet dict (was {} : false proof for `loads(s) == {}`).
    return nondet_dict(8)
