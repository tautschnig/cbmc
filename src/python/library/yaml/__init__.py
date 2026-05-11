"""
Verification model of the third-party `yaml` module
(PyYAML).

YAML parsing/emission is not modelled. Loads return empty
dict / list / None; dumps returns an empty string. The
API shape matches PyYAML's public interface.
"""


class YAMLError(Exception):
    pass


class MarkedYAMLError(YAMLError):
    pass


class ReaderError(YAMLError):
    pass


class ScannerError(MarkedYAMLError):
    pass


class ParserError(MarkedYAMLError):
    pass


class ComposerError(MarkedYAMLError):
    pass


class ConstructorError(MarkedYAMLError):
    pass


class RepresenterError(YAMLError):
    pass


class EmitterError(YAMLError):
    pass


class SerializerError(YAMLError):
    pass


# Loader/Dumper classes are referenced in type annotations;
# present as opaque placeholders.
class Loader:
    pass


class SafeLoader(Loader):
    pass


class FullLoader(Loader):
    pass


class UnsafeLoader(Loader):
    pass


class BaseLoader(Loader):
    pass


class Dumper:
    pass


class SafeDumper(Dumper):
    pass


class BaseDumper(Dumper):
    pass


def load(stream, Loader=None):
    return {}


def safe_load(stream):
    return {}


def load_all(stream, Loader=None):
    return []


def safe_load_all(stream):
    return []


def full_load(stream):
    return {}


def full_load_all(stream):
    return []


def unsafe_load(stream):
    return {}


def unsafe_load_all(stream):
    return []


def dump(data, stream=None, Dumper=None, **kwds) -> str:
    return ""


def safe_dump(data, stream=None, **kwds) -> str:
    return ""


def dump_all(documents, stream=None, Dumper=None, **kwds) -> str:
    return ""


def safe_dump_all(documents, stream=None, **kwds) -> str:
    return ""


def add_constructor(tag, constructor, Loader=None) -> None:
    return None


def add_representer(data_type, representer, Dumper=None) -> None:
    return None
