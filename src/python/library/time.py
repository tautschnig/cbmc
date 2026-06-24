"""
Verification model of the `time` module (partial).

All entries are nondet stand-ins. A future iteration could route
some of these (``monotonic_ns``, ``perf_counter_ns``) to the
corresponding C functions once CBMC's ansi-c library exposes the
matching signatures; the existing ``time(time_t *)`` and
``clock(void)`` in the library have signatures that don't fit our
``float``-returning Python stubs cleanly.
"""


# Common constants.
CLOCK_MONOTONIC: int = 1
CLOCK_MONOTONIC_RAW: int = 4
CLOCK_REALTIME: int = 0
CLOCK_PROCESS_CPUTIME_ID: int = 2
CLOCK_THREAD_CPUTIME_ID: int = 3
altzone: int = 0
daylight: int = 0
timezone: int = 0
tzname = ("UTC", "UTC")


def time() -> float:
    return nondet_float()


def monotonic() -> float:
    return nondet_float()


def monotonic_ns() -> int:
    return nondet_int()


def perf_counter() -> float:
    return nondet_float()


def perf_counter_ns() -> int:
    return nondet_int()


def process_time() -> float:
    return nondet_float()


def process_time_ns() -> int:
    return nondet_int()


def time_ns() -> int:
    return nondet_int()


def sleep(secs: float) -> None:
    return None


def gmtime(secs=None):
    return None


def localtime(secs=None):
    return None


def mktime(t) -> float:
    return 0.0


def asctime(t=None) -> str:
    return ""


def ctime(secs=None) -> str:
    return ""


def strftime(fmt: str, t=None) -> str:
    return ""


def strptime(s: str, fmt: str = ""):
    return None


def tzset() -> None:
    return None
