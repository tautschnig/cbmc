"""
Verification model of the `inspect` module.

Introspection on live objects is largely non-verification-
relevant. Every query function returns a conservative default:
``False`` for predicates, nondet for value extractors. This
over-approximation lets user code parse and run without
missing-symbol errors.
"""


# Predicates — all return False by default (nondet-like behaviour).
def isfunction(obj):
    return False


def ismethod(obj):
    return False


def isclass(obj):
    return False


def isbuiltin(obj):
    return False


def isroutine(obj):
    return False


def ismodule(obj):
    return False


def iscode(obj):
    return False


def isgenerator(obj):
    return False


def isgeneratorfunction(obj):
    return False


def iscoroutine(obj):
    return False


def iscoroutinefunction(obj):
    return False


def isawaitable(obj):
    return False


def isasyncgenfunction(obj):
    return False


def isasyncgen(obj):
    return False


def isabstract(obj):
    return False


def isdatadescriptor(obj):
    return False


def ismemberdescriptor(obj):
    return False


def isgetsetdescriptor(obj):
    return False


def istraceback(obj):
    return False


def isframe(obj):
    return False


# Value extractors
def getmembers(object, predicate=None):
    return []


def getmro(cls):
    return (cls,) if cls is not None else ()


def getdoc(object):
    return None


def getfile(object):
    return ""


def getmodule(object, _filename=None):
    return None


def getsource(object):
    return ""


def getsourcefile(object):
    return None


def getsourcelines(object):
    return ([], 0)


def getframeinfo(frame, context=1):
    return _FrameInfo()


def getargs(co):
    return _Arguments()


def getfullargspec(func):
    return _FullArgSpec()


def signature(obj, *, follow_wrapped=True, globals=None, locals=None,
              eval_str=False):
    return _Signature()


def classify_class_attrs(cls):
    return []


def getmembers_static(object, predicate=None):
    return []


class _FrameInfo:
    filename = ""
    lineno = 0
    function = ""
    code_context = None
    index = 0


class _Arguments:
    args = []
    varargs = None
    varkw = None


class _FullArgSpec:
    args = []
    varargs = None
    varkw = None
    defaults = None
    kwonlyargs = []
    kwonlydefaults = None
    annotations = {}


class Signature:
    empty = None

    def __init__(self, parameters=None, *, return_annotation=None):
        self.parameters = {}
        self.return_annotation = return_annotation


_Signature = Signature


class Parameter:
    POSITIONAL_ONLY = 0
    POSITIONAL_OR_KEYWORD = 1
    VAR_POSITIONAL = 2
    KEYWORD_ONLY = 3
    VAR_KEYWORD = 4
    empty = None

    def __init__(self, name, kind, *, default=None, annotation=None):
        self.name = name
        self.kind = kind
        self.default = default
        self.annotation = annotation


class BoundArguments:
    def __init__(self, signature=None, arguments=None):
        self.signature = signature
        self.arguments = arguments or {}

    def apply_defaults(self):
        return None


# Common constants
CO_OPTIMIZED = 0x1
CO_NEWLOCALS = 0x2
CO_VARARGS = 0x4
CO_VARKEYWORDS = 0x8
CO_NESTED = 0x10
CO_GENERATOR = 0x20
CO_NOFREE = 0x40
CO_COROUTINE = 0x80
CO_ITERABLE_COROUTINE = 0x100
CO_ASYNC_GENERATOR = 0x200
