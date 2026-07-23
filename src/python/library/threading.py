"""
Verification model of the `threading` module.

No concurrency is modelled; locks are no-ops and threads
execute their target sequentially on start (or not at all,
depending on caller expectations). Verification code should
not rely on actual concurrent interleaving through this
model — CBMC's symex is sequential.
"""


class ThreadError(Exception):
    pass


class BrokenBarrierError(RuntimeError):
    pass


class Thread:
    def __init__(self, group=None, target=None, name=None,
                 args=(), kwargs=None, *, daemon=None):
        self._target = target
        self._args = args
        self._kwargs = kwargs if kwargs is not None else {}
        self.name = name if name is not None else "Thread"
        self.daemon = daemon if daemon is not None else False
        self._started = False
        self._stopped = False

    def start(self) -> None:
        self._started = True
        # Invoke the target sequentially — sound for
        # verification of the target's single-threaded
        # behaviour.
        if self._target is not None:
            self._target(*self._args, **self._kwargs)
        self._stopped = True

    def run(self) -> None:
        if self._target is not None:
            self._target(*self._args, **self._kwargs)

    def join(self, timeout=None) -> None:
        return None

    def is_alive(self) -> bool:
        return self._started and not self._stopped

    def getName(self) -> str:
        return self.name

    def setName(self, name: str) -> None:
        self.name = name

    def setDaemon(self, daemonic: bool) -> None:
        self.daemon = daemonic

    def isDaemon(self) -> bool:
        return self.daemon


class Lock:
    def __init__(self):
        self._locked = False

    def acquire(self, blocking: bool = True, timeout: float = -1.0) -> bool:
        self._locked = True
        return True

    def release(self) -> None:
        # PLR / CPython: releasing an UNLOCKED lock raises RuntimeError
        # ('release unlocked lock') -- ESBMC github_4581_unlock_unheld.
        if not self._locked:
            raise RuntimeError("release unlocked lock")
        self._locked = False

    def locked(self) -> bool:
        return self._locked

    def __enter__(self):
        self.acquire()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.release()
        return False


class RLock(Lock):
    pass


class Semaphore:
    def __init__(self, value: int = 1):
        self._value = value

    def acquire(self, blocking: bool = True, timeout: float = -1.0) -> bool:
        return True

    def release(self, n: int = 1) -> None:
        return None

    def __enter__(self):
        self.acquire()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.release()
        return False


class BoundedSemaphore(Semaphore):
    pass


class Event:
    def __init__(self):
        self._flag = False

    def is_set(self) -> bool:
        return self._flag

    def set(self) -> None:
        self._flag = True

    def clear(self) -> None:
        self._flag = False

    def wait(self, timeout=None) -> bool:
        return self._flag


class Condition:
    def __init__(self, lock=None):
        self._lock = lock if lock is not None else Lock()

    def acquire(self, *args) -> bool:
        return True

    def release(self) -> None:
        return None

    def wait(self, timeout=None) -> bool:
        return True

    def wait_for(self, predicate, timeout=None) -> bool:
        return True

    def notify(self, n: int = 1) -> None:
        return None

    def notify_all(self) -> None:
        return None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        return False


class Barrier:
    def __init__(self, parties: int, action=None, timeout=None):
        self.parties = parties
        self.action = action

    def wait(self, timeout=None) -> int:
        return 0

    def reset(self) -> None:
        return None

    def abort(self) -> None:
        return None


class Timer(Thread):
    def __init__(self, interval: float, function, args=None, kwargs=None):
        super().__init__(target=function, args=args if args is not None else (),
                         kwargs=kwargs)
        self.interval = interval

    def cancel(self) -> None:
        return None


def current_thread():
    return Thread()


def active_count() -> int:
    return 1


def enumerate():
    return []


def main_thread():
    return Thread()


def get_ident() -> int:
    return 1


def get_native_id() -> int:
    return 1


local = type  # placeholder for threading.local
