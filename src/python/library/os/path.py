"""
Verification model of `os.path`.

Pure-Python path manipulation with results modelled as nondet
strings / bools.
"""

sep: str = "/"
altsep: str = ""
extsep: str = "."
pathsep: str = ":"
curdir: str = "."
pardir: str = ".."
devnull: str = "/dev/null"


def join(*paths: str) -> str:
    return ""


def split(path: str):
    return ("", "")


def splitext(path: str):
    return ("", "")


def splitdrive(path: str):
    return ("", "")


def basename(path: str) -> str:
    return ""


def dirname(path: str) -> str:
    return ""


def abspath(path: str) -> str:
    return ""


def realpath(path: str, *, strict: bool = False) -> str:
    return ""


def normpath(path: str) -> str:
    return ""


def relpath(path: str, start: str = ".") -> str:
    return ""


def commonpath(paths) -> str:
    return ""


def commonprefix(paths) -> str:
    return ""


def exists(path: str) -> bool:
    return False


def isfile(path: str) -> bool:
    return False


def isdir(path: str) -> bool:
    return False


def islink(path: str) -> bool:
    return False


def ismount(path: str) -> bool:
    return False


def lexists(path: str) -> bool:
    return False


def expanduser(path: str) -> str:
    return path


def expandvars(path: str) -> str:
    return path


def getsize(path: str) -> int:
    return 0


def getmtime(path: str) -> float:
    return 0.0


def getctime(path: str) -> float:
    return 0.0


def getatime(path: str) -> float:
    return 0.0


def samefile(path1: str, path2: str) -> bool:
    return False


def isabs(path: str) -> bool:
    return False
