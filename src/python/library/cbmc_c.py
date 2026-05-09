"""
C standard-library intrinsics for use from Python via
``@c_intrinsic``.

Users can ``from cbmc_c import strlen`` to call the named C
functions directly. Each entry is a one-line stub decorated
with ``@c_intrinsic``; the front-end lowers calls to the C
library at link-to-library time so CBMC's built-in model of
each routine applies.

Coverage is currently limited to functions whose signatures
use 64-bit ints and ``char *`` / ``size_t``, which match our
Python type projections exactly (Python ``int`` → signedbv 64,
Python ``str`` → ``char *``). Functions taking C ``int`` (32
bits) — the ctype predicates, ``toupper`` / ``tolower``,
``abs`` family, and ``atoi`` — aren't exposed here because the
C signature conflicts with our 64-bit projection and the
resulting re-declaration confuses the linker. A per-intrinsic
int-width annotation would unblock them; flagged as follow-up.
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


@c_intrinsic("strerror")
def strerror(errnum: int) -> str: ...


# ---------------------------------------------------------------
# <stdlib.h>
# ---------------------------------------------------------------
@c_intrinsic("getenv")
def getenv(name: str) -> str: ...
