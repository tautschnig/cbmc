# PLR / CPython: releasing an UNLOCKED threading.Lock raises RuntimeError
# ('release unlocked lock'). The stub tracked _locked but release() did
# not check it (ESBMC github_4581_unlock_unheld_fail). Must fail.
import threading

lock = threading.Lock()
lock.release()
