"""
Verification model of the third-party `click` CLI framework.

Decorator-based CLI definition. For verification, the
decorators are mostly no-ops — the decorated function is
returned as-is. Command-line dispatch is not modelled.
"""


class ClickException(Exception):
    def __init__(self, message: str = ""):
        self.message = message


class UsageError(ClickException):
    pass


class BadParameter(UsageError):
    pass


class MissingParameter(UsageError):
    pass


class Abort(RuntimeError):
    pass


class NoSuchOption(UsageError):
    pass


class ParamType:
    name = ""

    def convert(self, value, param, ctx):
        return value


class INT(ParamType):
    name = "integer"


class FLOAT(ParamType):
    name = "float"


class STRING(ParamType):
    name = "text"


class BOOL(ParamType):
    name = "boolean"


class UUID(ParamType):
    name = "uuid"


class Choice(ParamType):
    def __init__(self, choices, case_sensitive: bool = True):
        self.choices = choices


class File:
    def __init__(self, mode: str = "r", encoding=None, errors="strict",
                 lazy=None, atomic: bool = False):
        self.mode = mode


class Path:
    def __init__(self, exists: bool = False, file_okay: bool = True,
                 dir_okay: bool = True, writable: bool = False,
                 readable: bool = True, resolve_path: bool = False,
                 allow_dash: bool = False, path_type=None):
        self.exists = exists


class Context:
    def __init__(self, command=None, parent=None, info_name=None, obj=None):
        self.command = command
        self.parent = parent
        self.info_name = info_name
        self.obj = obj or {}
        self.params = {}

    def invoke(self, callback, *args, **kwargs):
        return callback(*args, **kwargs) if callable(callback) else None

    def forward(self, callback, *args, **kwargs):
        return callback(*args, **kwargs) if callable(callback) else None

    def exit(self, code: int = 0) -> None:
        return None

    def abort(self) -> None:
        raise Abort()


class Command:
    def __init__(self, name=None, callback=None, params=None, **kwargs):
        self.name = name
        self.callback = callback
        self.params = params or []

    def invoke(self, ctx):
        if self.callback is not None:
            return self.callback()
        return None

    def __call__(self, *args, **kwargs):
        if self.callback is not None:
            return self.callback(*args, **kwargs)
        return None


class Group(Command):
    def __init__(self, name=None, commands=None, **kwargs):
        super().__init__(name=name, **kwargs)
        self.commands = commands or {}

    def add_command(self, cmd, name=None):
        self.commands[name or cmd.name] = cmd

    def command(self, *args, **kwargs):
        def decorator(f):
            cmd = Command(name=f.__name__, callback=f)
            self.commands[cmd.name] = cmd
            return cmd
        return decorator

    def group(self, *args, **kwargs):
        def decorator(f):
            g = Group(name=f.__name__, callback=f)
            self.commands[g.name] = g
            return g
        return decorator


# Decorator factories. All return a decorator that
# simply returns the wrapped function/class unchanged.
def command(*args, **kwargs):
    def decorator(f):
        return Command(name=f.__name__, callback=f)
    return decorator


def group(*args, **kwargs):
    def decorator(f):
        return Group(name=f.__name__, callback=f)
    return decorator


def argument(*args, **kwargs):
    def decorator(f):
        return f
    return decorator


def option(*args, **kwargs):
    def decorator(f):
        return f
    return decorator


def pass_context(f):
    return f


def pass_obj(f):
    return f


def make_pass_decorator(object_type, ensure: bool = False):
    def decorator(f):
        return f
    return decorator


# Top-level helpers.
def echo(message=None, file=None, nl: bool = True, err: bool = False,
         color=None) -> None:
    return None


def secho(message=None, file=None, nl: bool = True, err: bool = False,
          color=None, **styles) -> None:
    return None


def style(text: str, **styles) -> str:
    return text


def prompt(text: str, default=None, hide_input: bool = False,
           confirmation_prompt: bool = False, type=None, value_proc=None,
           prompt_suffix: str = ": ", show_default: bool = True,
           err: bool = False, show_choices: bool = True):
    return default if default is not None else ""


def confirm(text: str, default: bool = False, abort: bool = False,
            prompt_suffix: str = ": ", show_default: bool = True,
            err: bool = False) -> bool:
    return default


def get_current_context(silent: bool = False):
    return Context()


def launch(url: str, wait: bool = False, locate: bool = False) -> int:
    return 0


def pause(info=None, err: bool = False) -> None:
    return None


def edit(text=None, editor=None, env=None, require_save: bool = True,
         extension: str = ".txt", filename=None):
    return text


def open_file(filename: str, mode: str = "r", encoding=None,
              errors: str = "strict", lazy: bool = False,
              atomic: bool = False):
    return None


def get_app_dir(app_name: str, roaming: bool = True,
                force_posix: bool = False) -> str:
    return ""


def format_filename(filename, shorten: bool = False) -> str:
    return str(filename)


def progressbar(iterable=None, length=None, label: str = "",
                show_eta: bool = True, show_percent=None,
                show_pos: bool = False, item_show_func=None,
                fill_char: str = "#", empty_char: str = "-",
                bar_template: str = "", info_sep: str = "  ",
                width: int = 36, file=None, color=None):
    return iterable if iterable is not None else []
