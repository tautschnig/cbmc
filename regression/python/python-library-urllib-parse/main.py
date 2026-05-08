# Exercises the default (library) import path for urllib.parse.
# The model lives at src/python/library/urllib/parse.py.
#
# The test is primarily a 'does it parse without Unknown method /
# no-body-for-callee errors' check — the actual values returned by
# the model are nondet (this is the point of a verification stub).

from urllib.parse import urlparse, urlsplit, urljoin, quote, unquote

parsed = urlparse("http://user:pw@example.com:8080/path?q=1#frag")
split = urlsplit("http://example.com/path")
joined = urljoin("http://a/", "b")
q = quote("value with spaces")
u = unquote("value%20with%20spaces")

# Read every documented ParseResult field to confirm each is a
# modelled attribute (not Unknown method / Cannot access).
_ = parsed.scheme
_ = parsed.netloc
_ = parsed.path
_ = parsed.params
_ = parsed.query
_ = parsed.fragment
_ = parsed.hostname
_ = parsed.port
_ = parsed.username
_ = parsed.password
_ = split.scheme
_ = split.netloc

# A verifiable property: ParseResult exposes geturl() as a method.
geturl_result = parsed.geturl()
_ = geturl_result

# Having reached this point without an 'Unknown method' / 'no body
# for callee' failure is the main assertion.
assert True
