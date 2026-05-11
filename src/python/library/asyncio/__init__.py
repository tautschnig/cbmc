"""
Verification model of the `asyncio` module.

Concurrency is collapsed: coroutines run sequentially
on demand, event loops are no-ops, futures resolve
synchronously. Suitable for code that uses async def
and await as control-flow decoration but doesn't rely
on actual concurrent scheduling.
"""


class CancelledError(Exception):
    pass


class InvalidStateError(Exception):
    pass


class TimeoutError(Exception):
    pass


class IncompleteReadError(Exception):
    pass


class LimitOverrunError(Exception):
    pass


class Future:
    def __init__(self, loop=None):
        self._result = None
        self._exception = None
        self._done = False

    def result(self):
        return self._result

    def exception(self):
        return self._exception

    def set_result(self, result) -> None:
        self._result = result
        self._done = True

    def set_exception(self, exception) -> None:
        self._exception = exception
        self._done = True

    def done(self) -> bool:
        return self._done

    def cancelled(self) -> bool:
        return False

    def cancel(self, msg=None) -> bool:
        return False

    def add_done_callback(self, callback, *, context=None) -> None:
        callback(self)


class Task(Future):
    def __init__(self, coro, *, loop=None, name=None):
        super().__init__(loop)
        self._coro = coro


class EventLoop:
    def __init__(self):
        self._running = False
        self._closed = False

    def run_forever(self) -> None:
        return None

    def run_until_complete(self, future):
        if callable(future):
            return future()
        return future

    def stop(self) -> None:
        self._running = False

    def close(self) -> None:
        self._closed = True

    def is_running(self) -> bool:
        return self._running

    def is_closed(self) -> bool:
        return self._closed

    def create_task(self, coro, *, name=None):
        return Task(coro)

    def create_future(self):
        return Future()

    def call_soon(self, callback, *args, context=None):
        callback(*args)

    def call_later(self, delay, callback, *args, context=None):
        callback(*args)


def run(coro, *, debug=None):
    # Synchronous execution: evaluate the coroutine by
    # invoking it like a regular function. Our frontend
    # treats `async def` like `def`, so this works.
    if callable(coro):
        return coro()
    return coro


def create_task(coro, *, name=None):
    return Task(coro)


def ensure_future(coro_or_future, *, loop=None):
    if isinstance(coro_or_future, Future):
        return coro_or_future
    return Task(coro_or_future)


async def sleep(delay: float, result=None):
    return result


async def wait(fs, *, timeout=None, return_when=None):
    return (set(), set())


async def wait_for(fut, timeout):
    if callable(fut):
        return fut()
    return fut


async def gather(*coros_or_futures, return_exceptions: bool = False):
    results = []
    for c in coros_or_futures:
        if callable(c):
            results.append(c())
        else:
            results.append(c)
    return results


async def shield(aw):
    if callable(aw):
        return aw()
    return aw


def get_event_loop():
    return EventLoop()


def new_event_loop():
    return EventLoop()


def set_event_loop(loop) -> None:
    return None


def get_running_loop():
    return EventLoop()


class Queue:
    def __init__(self, maxsize: int = 0, *, loop=None):
        self._items = []
        self.maxsize = maxsize

    async def put(self, item) -> None:
        self._items.append(item)

    async def get(self):
        return self._items.pop(0) if self._items else None

    def put_nowait(self, item) -> None:
        self._items.append(item)

    def get_nowait(self):
        return self._items.pop(0) if self._items else None

    def qsize(self) -> int:
        return len(self._items)

    def empty(self) -> bool:
        return len(self._items) == 0

    def full(self) -> bool:
        return self.maxsize > 0 and len(self._items) >= self.maxsize


class Lock:
    def __init__(self, *, loop=None):
        self._locked = False

    async def acquire(self) -> bool:
        self._locked = True
        return True

    def release(self) -> None:
        self._locked = False

    def locked(self) -> bool:
        return self._locked
