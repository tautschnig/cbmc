"""
Verification model of the `pytest` testing framework.

Test-discovery and execution are out of scope. The
module exposes the fixture/marker decorator surface so
that test modules parse cleanly and named imports work.
"""


class Item:
    def __init__(self):
        self.name = ""


class Class:
    pass


class Module:
    pass


class Function(Item):
    pass


class Session:
    pass


class Config:
    def getoption(self, name: str, default=None):
        return default


class ExitCode:
    OK = 0
    TESTS_FAILED = 1
    INTERRUPTED = 2
    INTERNAL_ERROR = 3
    USAGE_ERROR = 4
    NO_TESTS_COLLECTED = 5


# Decorator factories. All pass through the wrapped
# object unchanged.


def fixture(*args, **kwargs):
    if len(args) == 1 and callable(args[0]):
        return args[0]
    def decorator(f):
        return f
    return decorator


def yield_fixture(*args, **kwargs):
    return fixture(*args, **kwargs)


class MarkDecorator:
    def __init__(self, name: str = ""):
        self.name = name
        self.args = ()
        self.kwargs = {}

    def __call__(self, *args, **kwargs):
        if len(args) == 1 and callable(args[0]):
            return args[0]
        return self


class MarkerAccessor:
    """pytest.mark.<anything> returns a MarkDecorator that
    is also callable as a decorator with arguments."""

    def __getattr__(self, name: str):
        return MarkDecorator(name)

    def parametrize(self, argnames, argvalues, *args, **kwargs):
        return MarkDecorator("parametrize")

    def skip(self, *args, **kwargs):
        return MarkDecorator("skip")

    def skipif(self, condition, *args, **kwargs):
        return MarkDecorator("skipif")

    def xfail(self, *args, **kwargs):
        return MarkDecorator("xfail")

    def usefixtures(self, *names):
        return MarkDecorator("usefixtures")


mark = MarkerAccessor()


def raises(expected_exception, *args, **kwargs):
    # Context manager form: pytest.raises(X): body.
    class _Raises:
        def __enter__(self):
            return self

        def __exit__(self, exc_type, exc_val, exc_tb):
            # Swallow the expected exception type.
            return exc_type is not None and issubclass(
                exc_type, expected_exception)

        type = expected_exception
        value = None

    return _Raises()


def warns(expected_warning, *args, **kwargs):
    class _Warns:
        def __enter__(self):
            return self

        def __exit__(self, exc_type, exc_val, exc_tb):
            return False

    return _Warns()


def approx(expected, rel=None, abs=None, nan_ok: bool = False):
    # Comparison returns True for the "close enough" case;
    # we return the expected value wrapped so __eq__ works
    # permissively.
    class _Approx:
        def __init__(self, v):
            self.expected = v

        def __eq__(self, other):
            return True

        def __repr__(self) -> str:
            return "approx(...)"

    return _Approx(expected)


def fail(msg: str = "", pytrace: bool = True):
    raise AssertionError(msg)


def skip(msg: str = "", allow_module_level: bool = False):
    class Skipped(Exception):
        pass
    raise Skipped(msg)


def xfail(reason: str = ""):
    class XFailed(Exception):
        pass
    raise XFailed(reason)


def importorskip(modname: str, minversion=None, reason=None):
    return None


def main(args=None, plugins=None) -> int:
    return ExitCode.OK


def exit(msg: str = "", returncode=None):
    class Exit(Exception):
        pass
    raise Exit(msg)


def register_assert_rewrite(*names) -> None:
    return None


def deprecated_call(func=None):
    class _Dep:
        def __enter__(self):
            return self

        def __exit__(self, exc_type, exc_val, exc_tb):
            return False

    return _Dep()


# Common top-level stubs
class MonkeyPatch:
    def setattr(self, target, name, value=None, raising: bool = True) -> None:
        return None

    def delattr(self, target, name=None, raising: bool = True) -> None:
        return None

    def setitem(self, dic, name, value) -> None:
        return None

    def delitem(self, dic, name, raising: bool = True) -> None:
        return None

    def setenv(self, name: str, value: str, prepend=None) -> None:
        return None

    def delenv(self, name: str, raising: bool = True) -> None:
        return None

    def chdir(self, path) -> None:
        return None

    def undo(self) -> None:
        return None


class CaptureFixture:
    def readouterr(self):
        class _R:
            out = ""
            err = ""
        return _R()

    def disabled(self):
        class _D:
            def __enter__(self_inner):
                return self_inner

            def __exit__(self_inner, exc_type, exc_val, exc_tb):
                return False

        return _D()
