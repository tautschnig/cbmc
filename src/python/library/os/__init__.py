"""
Verification model of the `os` module (partial).

Common entry points only. Values are modelled as nondet stand-ins
for the filesystem-changing calls and as plain constants for the
path/system constants.

`@c_intrinsic` routing is intentionally not used yet for the POSIX
syscalls in this module: getpid/getuid/getenv are either not
present in CBMC's ansi-c library (getpid, getuid) or have a C
signature (char *) that doesn't match our Python-str representation
(getenv). A future iteration will add the missing library bodies
and the Python-to-C string marshalling needed for getenv.
"""


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


def mkdir(path: str, mode: int = 0) -> None:
    return None


def makedirs(path: str, mode: int = 0, exist_ok: bool = False) -> None:
    return None


def rmdir(path: str) -> None:
    return None


def remove(path: str) -> None:
    return None


def rename(src: str, dst: str) -> None:
    return None


def listdir(path: str = ""):
    return []


# os.path is provided by src/python/library/os/path.py.
