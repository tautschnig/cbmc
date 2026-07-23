# No-false-positive guard: a properly acquired lock releases cleanly
# (no spurious release-unheld RuntimeError), and the context-manager
# form does not spuriously fault on exit.
import threading


def explicit() -> None:
    lock = threading.Lock()
    lock.acquire()
    lock.release()
    assert not lock.locked()


def ctx() -> None:
    lock = threading.Lock()
    with lock:
        pass


explicit()
ctx()
assert True
