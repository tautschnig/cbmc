"""
Verification model of the `traceback` module.

All formatting functions return empty strings / empty lists.
Exception introspection is non-verification-critical; user
code that inspects or formats traceback objects gets
over-approximated empty results.
"""


def print_tb(tb, limit=None, file=None):
    return None


def print_exception(exc, /, value=None, tb=None, limit=None, file=None,
                    chain=True):
    return None


def print_exc(limit=None, file=None, chain=True):
    return None


def print_last(limit=None, file=None, chain=True):
    return None


def print_stack(f=None, limit=None, file=None):
    return None


def format_tb(tb, limit=None):
    return []


def format_exception(exc, /, value=None, tb=None, limit=None, chain=True):
    return []


def format_exception_only(exc, /, value=None, show_group=False):
    return []


def format_exc(limit=None, chain=True):
    return ""


def format_stack(f=None, limit=None):
    return []


def extract_tb(tb, limit=None):
    return []


def extract_stack(f=None, limit=None):
    return []


def clear_frames(tb):
    return None


def walk_tb(tb):
    return iter([])


def walk_stack(f):
    return iter([])


class FrameSummary:
    def __init__(self, filename, lineno, name, *, lookup_line=True,
                 locals=None, line=None, end_lineno=None, colno=None,
                 end_colno=None):
        self.filename = filename
        self.lineno = lineno
        self.name = name


class TracebackException:
    def __init__(self, exc_type, exc_value, exc_traceback, *, limit=None,
                 lookup_lines=True, capture_locals=False, compact=False,
                 max_group_width=15, max_group_depth=10, _seen=None):
        self.exc_type = exc_type

    def format(self, *, chain=True, _ctx=None):
        return iter([])

    def format_exception_only(self, *, show_group=False, _depth=0):
        return iter([])

    @classmethod
    def from_exception(cls, exc, **kwargs):
        return cls(type(exc), exc, exc.__traceback__)


class StackSummary(list):
    @classmethod
    def extract(cls, frame_gen, *, limit=None, lookup_lines=True,
                capture_locals=False):
        return cls()

    @classmethod
    def from_list(cls, a_list):
        return cls(a_list)

    def format(self):
        return []

    def format_frame_summary(self, frame_summary):
        return ""
