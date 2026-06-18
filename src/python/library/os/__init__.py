"""
Verification model of the `os` module (partial).

Common entry points only. Values are modelled as nondet stand-ins
for the filesystem-changing calls, as plain constants for the
path/system constants. ``@c_intrinsic`` routing is in place
conceptually (see python_convertert's str marshalling), but the
POSIX calls we'd like to route (``getenv``, ``getcwd``, …) trip
the C library's dereference checks because Python's refined-
string data pointer lives in the string-refinement array space
rather than on a genuine heap object; routing them will need a
dedicated str→heap materialisation step.
"""

from __cbmc__ import may_raise


# Path separator / line separator constants (Linux defaults).
sep: str = "/"
altsep: str = ""
extsep: str = "."
pathsep: str = ":"
linesep: str = "\n"
curdir: str = "."
pardir: str = ".."
devnull: str = "/dev/null"


# environ is represented as a nondet dict. Writes to ``environ``
# by the Python program are not propagated across ``os.getenv``
# — this is a known under-approximation of CPython semantics.
environ: dict = {}


# Marshalling Python ``str`` → C ``char *`` is in place (see
# python_convertert's @c_intrinsic dispatch), but the C getenv
# model dereferences its argument to assert zero-termination and
# that fails because the refined-string data pointer lives in the
# string-refinement array space, not a genuine heap object. For
# now we return a plain nondet string; an eventual fix will
# materialise the argument into a heap-allocated buffer before
# calling C getenv.
def getenv(key: str, default=None) -> str:
    return ""


def getpid() -> int:
    return 0


def getppid() -> int:
    return 0


def getuid() -> int:
    return 0


def geteuid() -> int:
    return 0


def getgid() -> int:
    return 0


def getegid() -> int:
    return 0


def getcwd() -> str:
    return ""


def chdir(path: str) -> None:
    return None


@may_raise("OSError")
def mkdir(path: str, mode: int = 0) -> None:
    return None


@may_raise("OSError")
def makedirs(path: str, mode: int = 0, exist_ok: bool = False) -> None:
    return None


@may_raise("OSError")
def rmdir(path: str) -> None:
    return None


@may_raise("OSError")
def remove(path: str) -> None:
    return None


@may_raise("OSError")
def rename(src: str, dst: str) -> None:
    return None


def listdir(path: str = ""):
    return []


# os.path is provided by src/python/library/os/path.py.
