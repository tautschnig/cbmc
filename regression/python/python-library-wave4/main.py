# Smoke test for the session's new library stubs: logging,
# warnings, abc, io, string, traceback, inspect. Each module
# imports cleanly and its minimal call surface is reachable.

# logging
from logging import getLogger, INFO, basicConfig
log = getLogger("my_module")
log.info("hello")
log.setLevel(INFO)

# warnings
from warnings import warn, filterwarnings, catch_warnings
warn("test warning")
with catch_warnings(record=True) as w:
    pass

# abc
from abc import ABC, abstractmethod


class Shape(ABC):
    @abstractmethod
    def area(self) -> float: ...


# io
from io import StringIO, BytesIO
s = StringIO("hello")
b = BytesIO(b"world")
_ = s.getvalue()

# string
from string import ascii_letters, digits, Template
# Module-level imports work — len() on these imports currently
# returns nondet due to how library-loaded constants bind, so
# we just smoke-test the Template class.
t = Template("$x")
_ = t.substitute(x="world")

# traceback
from traceback import format_exc, format_exception

# inspect
from inspect import isfunction, signature, Parameter
