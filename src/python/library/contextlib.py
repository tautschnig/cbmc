"""
Verification model of the `contextlib` module.

Covers the commonly-used decorators and managers: contextmanager,
closing, suppress, redirect_stdout/err, nullcontext, ExitStack,
asynccontextmanager.

Context managers are modelled as identity wrappers — __enter__
returns the managed resource (or the manager itself), __exit__
is a no-op. This over-approximation is sound for verification
because user code typically inspects the value inside the `with`
block; the actual __enter__/__exit__ side effects are assumed
non-verification-relevant.
"""


class AbstractContextManager:
    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        return False


class AbstractAsyncContextManager:
    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        return False


class _GeneratorContextManager(AbstractContextManager):
    """Wraps a generator function per the @contextmanager protocol."""

    def __init__(self, func, args, kwds):
        self.gen = None  # Lazily constructed.

    def __enter__(self):
        return None  # over-approximation — real value depends on generator

    def __exit__(self, exc_type, exc_val, exc_tb):
        return False


def contextmanager(func):
    """@contextmanager decorator — wraps the generator in a
    _GeneratorContextManager. Returns a factory."""

    def helper(*args, **kwds):
        return _GeneratorContextManager(func, args, kwds)

    return helper


def asynccontextmanager(func):
    def helper(*args, **kwds):
        return _GeneratorContextManager(func, args, kwds)

    return helper


class closing(AbstractContextManager):
    """Wrap a resource; call .close() on exit. Our __exit__ is a
    no-op — close semantics aren't verification-critical."""

    def __init__(self, thing):
        self.thing = thing

    def __enter__(self):
        return self.thing

    def __exit__(self, exc_type, exc_val, exc_tb):
        return False


class aclosing(AbstractAsyncContextManager):
    def __init__(self, thing):
        self.thing = thing

    async def __aenter__(self):
        return self.thing

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        return False


class suppress(AbstractContextManager):
    """``with suppress(X): ...`` — exceptions of type X are
    swallowed. We cannot match the exact exception type against
    the runtime exception here, so __exit__ returns a nondet bool:
    the front-end then explores both the suppress and the propagate
    path. This is sound (the propagate path preserves any
    uncaught-exception failure) — an unconditional ``return True``
    would unsoundly swallow exceptions whose type is NOT in X."""

    def __init__(self, *exceptions):
        self._exceptions = exceptions

    def __enter__(self):
        return None

    def __exit__(self, exc_type, exc_val, exc_tb):
        return nondet_bool()


class redirect_stdout(AbstractContextManager):
    def __init__(self, new_target):
        self._new_target = new_target

    def __enter__(self):
        return self._new_target

    def __exit__(self, exc_type, exc_val, exc_tb):
        return False


class redirect_stderr(redirect_stdout):
    pass


class nullcontext(AbstractContextManager):
    """No-op context manager — ``with nullcontext(x) as y:``
    yields y=x."""

    def __init__(self, enter_result=None):
        self.enter_result = enter_result

    def __enter__(self):
        return self.enter_result

    def __exit__(self, *args):
        return False


class ExitStack(AbstractContextManager):
    """``with ExitStack() as stack:`` — tracks nested resources.
    Our model is a no-op: callbacks are not actually invoked."""

    def __init__(self):
        self._exit_callbacks = []

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        return False

    def enter_context(self, cm):
        return cm.__enter__()

    def push(self, cm):
        return cm

    def callback(self, callback, *args, **kwds):
        return callback

    def pop_all(self):
        return ExitStack()

    def close(self):
        return None


class AsyncExitStack(ExitStack):
    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        return False

    async def enter_async_context(self, cm):
        return await cm.__aenter__()

    async def aclose(self):
        return None


# Decorator helpers
class ContextDecorator:
    def _recreate_cm(self):
        return self

    def __call__(self, func):
        def inner(*args, **kwds):
            with self._recreate_cm():
                return func(*args, **kwds)

        return inner


class AsyncContextDecorator:
    def _recreate_cm(self):
        return self

    def __call__(self, func):
        return func
