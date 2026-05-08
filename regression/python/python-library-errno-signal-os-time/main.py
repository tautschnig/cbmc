# Exercises the library's errno/signal/os/os.path/time/cmath stubs.
# These are all nondet stand-ins or constants; the test just
# verifies that every listed name resolves through the model
# library without 'no body for callee' or 'Unknown variable'
# failures.

from errno import EINVAL, ENOENT, EACCES, EEXIST
from signal import SIGKILL, SIGTERM, SIGINT, SIGABRT
from os import getenv, getpid, getuid, sep, environ
from os.path import join, exists, basename, dirname, isfile, isdir
from time import time, monotonic, sleep, perf_counter
from cmath import pi, e

# errno / signal constants come from the host system's headers and
# are checked by value.
assert EINVAL == 22
assert ENOENT == 2
assert EACCES == 13
assert EEXIST == 17
assert SIGKILL == 9
assert SIGTERM == 15
assert SIGINT == 2
assert SIGABRT == 6

# os helpers — nondet but type-correct.
_ = getenv("PATH")
_ = getpid()
_ = getuid()
_ = sep

# os.path — nondet but type-correct.
p = join("/tmp", "foo", "bar")
_ = exists(p)
_ = basename(p)
_ = dirname(p)
_ = isfile(p)
_ = isdir(p)

# time — nondet numbers; sleep is a no-op.
_ = time()
_ = monotonic()
_ = perf_counter()
sleep(0.1)

# cmath — constants via 'from cmath import' form.
_ = pi
_ = e

assert True
