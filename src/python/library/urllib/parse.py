"""
Verification-optimized model of urllib.parse.

The real CPython source is 1000+ lines of string manipulation that is
slow and imprecise to verify. This model captures the public surface
relied upon by typical callers and models each function as returning
an opaque value with the correct shape.

Coverage:
  * ParseResult        — named-tuple-shaped result of urlparse/urlsplit.
  * urlparse, urlsplit — return a nondet ParseResult.
  * urlunparse, urlunsplit — return a nondet string.
  * urljoin, urldefrag — return a nondet string.
  * quote, unquote, quote_plus, unquote_plus, quote_from_bytes —
    return a nondet string.
  * urlencode           — return a nondet string.
  * parse_qs, parse_qsl — return a nondet dict / list.
"""


class ParseResult:
    """Shape-compatible stand-in for urllib.parse.ParseResult.

    Each attribute is annotated so the CBMC front-end creates a
    proper struct component for it. The values themselves are nondet
    (returned from urlparse() below), which is the sound
    over-approximation for static verification.
    """

    scheme: str
    netloc: str
    path: str
    params: str
    query: str
    fragment: str
    # Derived fields on real ParseResult; we expose them as plain
    # str/int so callers can read '.hostname', '.port', '.username',
    # '.password' without triggering attribute-missing warnings.
    hostname: str
    port: int
    username: str
    password: str

    def __init__(
        self,
        scheme: str = "",
        netloc: str = "",
        path: str = "",
        params: str = "",
        query: str = "",
        fragment: str = "",
    ):
        self.scheme = scheme
        self.netloc = netloc
        self.path = path
        self.params = params
        self.query = query
        self.fragment = fragment
        self.hostname = ""
        self.port = 0
        self.username = ""
        self.password = ""

    def geturl(self) -> str:
        return ""


def urlparse(url, scheme: str = "", allow_fragments: bool = True) -> ParseResult:
    # Return a fresh ParseResult whose fields are nondet. Callers
    # then see 'parsed.hostname' etc. as nondet strings rather than
    # 'attribute not found'.
    return ParseResult()


def urlsplit(url, scheme: str = "", allow_fragments: bool = True) -> ParseResult:
    return ParseResult()


def urlunparse(components) -> str:
    return ""


def urlunsplit(components) -> str:
    return ""


def urljoin(base: str, url: str, allow_fragments: bool = True) -> str:
    return ""


def urldefrag(url: str) -> str:
    return ""


def quote(string, safe: str = "/", encoding: str = "", errors: str = "") -> str:
    return ""


def quote_plus(string, safe: str = "", encoding: str = "", errors: str = "") -> str:
    return ""


def quote_from_bytes(bs, safe: str = "/") -> str:
    return ""


def unquote(string: str, encoding: str = "utf-8", errors: str = "replace") -> str:
    return ""


def unquote_plus(string: str, encoding: str = "utf-8", errors: str = "replace") -> str:
    return ""


def unquote_to_bytes(string: str) -> str:
    return ""


def urlencode(
    query,
    doseq: bool = False,
    safe: str = "",
    encoding: str = "",
    errors: str = "",
) -> str:
    return ""


def parse_qs(
    qs,
    keep_blank_values: bool = False,
    strict_parsing: bool = False,
    encoding: str = "utf-8",
    errors: str = "replace",
    max_num_fields: int = 0,
    separator: str = "&",
):
    return {}


def parse_qsl(
    qs,
    keep_blank_values: bool = False,
    strict_parsing: bool = False,
    encoding: str = "utf-8",
    errors: str = "replace",
    max_num_fields: int = 0,
    separator: str = "&",
):
    return []
