"""
C standard-library intrinsics for use from Python via
``@c_intrinsic``.

Users can ``from cbmc_c import strlen, toupper`` to call the
named C functions directly. Each entry is a one-line stub
decorated with ``@c_intrinsic``; the front-end lowers calls
to the C library at link-to-library time so CBMC's built-in
model of each routine applies.

Functions declared with ``int_width=32`` take or return a C
``int`` (32 bits) rather than Python's default 64-bit
``int``. The frontend transparently narrows int arguments and
widens int results so callers still work with 64-bit Python
ints end-to-end.
"""

from __cbmc__ import c_intrinsic


# ---------------------------------------------------------------
# <string.h> — functions with char */size_t signatures.
# ---------------------------------------------------------------
@c_intrinsic("strlen")
def strlen(s: str) -> int: ...


@c_intrinsic("strcmp")
def strcmp(a: str, b: str) -> int: ...


@c_intrinsic("strncmp")
def strncmp(a: str, b: str, n: int) -> int: ...


@c_intrinsic("strcasecmp")
def strcasecmp(a: str, b: str) -> int: ...


@c_intrinsic("strncasecmp")
def strncasecmp(a: str, b: str, n: int) -> int: ...


@c_intrinsic("strstr")
def strstr(haystack: str, needle: str) -> str: ...


@c_intrinsic("strerror", int_width=32)
def strerror(errnum: int) -> str: ...


# ---------------------------------------------------------------
# <stdlib.h>
# ---------------------------------------------------------------
@c_intrinsic("getenv")
def getenv(name: str) -> str: ...


@c_intrinsic("abs", int_width=32)
def abs(n: int) -> int: ...


@c_intrinsic("labs")
def labs(n: int) -> int: ...


@c_intrinsic("llabs")
def llabs(n: int) -> int: ...


@c_intrinsic("atoi", int_width=32)
def atoi(s: str) -> int: ...


@c_intrinsic("atol")
def atol(s: str) -> int: ...


@c_intrinsic("atoll")
def atoll(s: str) -> int: ...


# ---------------------------------------------------------------
# <ctype.h>
#
# All take and return C int (32 bits) — declared with
# int_width=32 so the frontend projects Python ``int`` to
# ``signedbv 32`` for these specifically.
# ---------------------------------------------------------------
@c_intrinsic("isalnum", int_width=32)
def isalnum(c: int) -> int: ...


@c_intrinsic("isalpha", int_width=32)
def isalpha(c: int) -> int: ...


@c_intrinsic("iscntrl", int_width=32)
def iscntrl(c: int) -> int: ...


@c_intrinsic("isdigit", int_width=32)
def isdigit(c: int) -> int: ...


@c_intrinsic("isgraph", int_width=32)
def isgraph(c: int) -> int: ...


@c_intrinsic("islower", int_width=32)
def islower(c: int) -> int: ...


@c_intrinsic("isprint", int_width=32)
def isprint(c: int) -> int: ...


@c_intrinsic("ispunct", int_width=32)
def ispunct(c: int) -> int: ...


@c_intrinsic("isspace", int_width=32)
def isspace(c: int) -> int: ...


@c_intrinsic("isupper", int_width=32)
def isupper(c: int) -> int: ...


@c_intrinsic("isxdigit", int_width=32)
def isxdigit(c: int) -> int: ...


@c_intrinsic("tolower", int_width=32)
def tolower(c: int) -> int: ...


@c_intrinsic("toupper", int_width=32)
def toupper(c: int) -> int: ...
