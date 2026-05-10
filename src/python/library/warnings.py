"""
Verification model of the `warnings` module.

Every emission call (``warn`` etc.) is a no-op. Filters,
catch_warnings context-manager, and formatters are stubbed so
user code that sets up or uses warning filters runs without
error.
"""


def warn(message, category=None, stacklevel=1, source=None):
    return None


def warn_explicit(message, category, filename, lineno, module=None,
                  registry=None, module_globals=None, source=None):
    return None


def showwarning(message, category, filename, lineno, file=None, line=None):
    return None


def formatwarning(message, category, filename, lineno, line=None):
    return ""


def filterwarnings(action, message="", category=None, module="", lineno=0,
                   append=False):
    return None


def simplefilter(action, category=None, lineno=0, append=False):
    return None


def resetwarnings():
    return None


class catch_warnings:
    def __init__(self, *, record: bool = False):
        self.record = record
        self.log = []

    def __enter__(self):
        return self.log if self.record else None

    def __exit__(self, exc_type, exc_val, exc_tb):
        return False


filters = []


# Standard categories
class WarningMessage:
    def __init__(self, message=None, category=None, filename="", lineno=0,
                 file=None, line=None, source=None):
        self.message = message
        self.category = category


# Category aliases (CPython exposes built-in Warning subclasses).
# We re-export the built-ins here; user code like ``from warnings
# import DeprecationWarning`` is rare but present.
DeprecationWarning = DeprecationWarning
PendingDeprecationWarning = PendingDeprecationWarning
UserWarning = UserWarning
SyntaxWarning = SyntaxWarning
RuntimeWarning = RuntimeWarning
FutureWarning = FutureWarning
ImportWarning = ImportWarning
UnicodeWarning = UnicodeWarning
BytesWarning = BytesWarning
ResourceWarning = ResourceWarning
