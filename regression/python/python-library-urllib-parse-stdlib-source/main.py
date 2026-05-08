# Exercises --python-use-stdlib-source by forcing the front-end to
# resolve urllib.parse through the system Python installation rather
# than through our model library.
#
# The CPython source (~1200 lines) is imported, parsed, and its
# functions become nondet-returning callables. This test verifies
# that the code path works and that our quiet-by-default warning
# policy keeps verification tractable.

from urllib.parse import urlparse, urlsplit

# Calls must resolve. In stdlib-source mode the return value is an
# over-approximation (our front-end doesn't fully model the CPython
# implementation); the important observable property is that the
# call does not trip 'no body for callee urlparse'.
_ = urlparse("http://example.com")
_ = urlsplit("http://example.com")

assert True
