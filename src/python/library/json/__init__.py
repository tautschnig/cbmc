"""
Verification-optimized model of the `json` module.

Exposes loads, dumps, load, dump as nondet-returning stubs. The
CPython implementation is substantial and performance-sensitive;
for verification we approximate the round-trip as nondet-on-parse.
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
        return ""

    def iterencode(self, o, _one_shot: bool = False):
        return []


class JSONDecoder:
    def __init__(self, *, object_hook=None, parse_float=None,
                 parse_int=None, parse_constant=None, strict: bool = True,
                 object_pairs_hook=None):
        return None

    def decode(self, s: str, _w=None):
        return None

    def raw_decode(self, s: str, idx: int = 0):
        return (None, 0)


def dump(obj, fp, *, skipkeys: bool = False, ensure_ascii: bool = True,
         check_circular: bool = True, allow_nan: bool = True,
         cls=None, indent=None, separators=None, default=None,
         sort_keys: bool = False, **kw) -> None:
    return None


def dumps(obj, *, skipkeys: bool = False, ensure_ascii: bool = True,
          check_circular: bool = True, allow_nan: bool = True, cls=None,
          indent=None, separators=None, default=None,
          sort_keys: bool = False, **kw) -> str:
    return ""


def load(fp, *, cls=None, object_hook=None, parse_float=None,
         parse_int=None, parse_constant=None, object_pairs_hook=None,
         **kw):
    # Nondet JSON value; caller typically treats the result as a
    # dict or list.
    return {}


def loads(s, *, cls=None, object_hook=None, parse_float=None,
          parse_int=None, parse_constant=None, object_pairs_hook=None,
          **kw):
    return {}
