"""
Verification model of the `argparse` module.

Covers the common surface: ``ArgumentParser``,
``add_argument``, ``parse_args``, ``Namespace``, and a handful
of the most-used actions and types.

The parser is not actually executed — ``parse_args`` returns a
``Namespace`` whose attribute reads produce nondet values, which
is the correct over-approximation for verification (user code
can't rely on argparse returning specific values).
"""


class _Nondet:
    """Placeholder — any attribute read yields a nondet-like value
    via the front-end's fallthrough."""

    def __getattr__(self, name):
        return None


class Namespace:
    """``argparse.Namespace`` — attribute container. The front-end
    treats reads of undeclared attributes as nondet."""

    def __init__(self, **kwargs):
        for k, v in kwargs.items():
            setattr(self, k, v)

    def __repr__(self):
        return "Namespace()"

    def __eq__(self, other):
        return False

    def __contains__(self, key):
        return True


class Action:
    """Base class — model most actions' ``__call__`` as a no-op."""

    def __init__(self, option_strings=None, dest=None, **kwargs):
        self.option_strings = option_strings or []
        self.dest = dest

    def __call__(self, parser, namespace, values, option_string=None):
        return None


class ArgumentParser:
    """``argparse.ArgumentParser`` — accepts add_argument etc. and
    returns a Namespace on parse_args."""

    def __init__(self, prog=None, usage=None, description=None,
                 epilog=None, parents=None, formatter_class=None,
                 prefix_chars="-", fromfile_prefix_chars=None,
                 argument_default=None, conflict_handler="error",
                 add_help=True, allow_abbrev=True, exit_on_error=True):
        self.prog = prog
        self.description = description
        self._actions = []

    def add_argument(self, *args, **kwargs):
        action = Action(option_strings=list(args),
                        dest=kwargs.get("dest"))
        self._actions.append(action)
        return action

    def add_argument_group(self, *args, **kwargs):
        return self

    def add_mutually_exclusive_group(self, required=False):
        return self

    def add_subparsers(self, **kwargs):
        return _Subparsers()

    def parse_args(self, args=None, namespace=None):
        return namespace or Namespace()

    def parse_known_args(self, args=None, namespace=None):
        return (namespace or Namespace(), [])

    def parse_intermixed_args(self, args=None, namespace=None):
        return namespace or Namespace()

    def parse_known_intermixed_args(self, args=None, namespace=None):
        return (namespace or Namespace(), [])

    def error(self, message):
        raise SystemExit(2)

    def exit(self, status=0, message=None):
        raise SystemExit(status)

    def print_help(self, file=None):
        pass

    def print_usage(self, file=None):
        pass

    def format_help(self):
        return ""

    def format_usage(self):
        return ""

    def set_defaults(self, **kwargs):
        return None

    def get_default(self, dest):
        return None


class _Subparsers:
    """Returned by add_subparsers; supports add_parser."""

    def add_parser(self, name, **kwargs):
        return ArgumentParser()


# Commonly-used sentinels / formatter classes — all no-ops.
class HelpFormatter:
    pass


class RawDescriptionHelpFormatter(HelpFormatter):
    pass


class RawTextHelpFormatter(HelpFormatter):
    pass


class ArgumentDefaultsHelpFormatter(HelpFormatter):
    pass


class MetavarTypeHelpFormatter(HelpFormatter):
    pass


# Exceptions
class ArgumentError(Exception):
    pass


class ArgumentTypeError(Exception):
    pass


# Constants
SUPPRESS = "==SUPPRESS=="
OPTIONAL = "?"
ZERO_OR_MORE = "*"
ONE_OR_MORE = "+"
REMAINDER = "..."
PARSER = "A..."
