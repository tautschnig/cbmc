"""
Verification model of the `string` module.

The module exposes a handful of constants (ASCII character sets,
digits, hex / octal digits, printable / whitespace) plus the
``Template``, ``Formatter`` and ``capwords`` helpers. We expose
them as static strings / no-op classes.
"""


ascii_lowercase = "abcdefghijklmnopqrstuvwxyz"
ascii_uppercase = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
ascii_letters = ascii_lowercase + ascii_uppercase
digits = "0123456789"
hexdigits = digits + "abcdef" + "ABCDEF"
octdigits = "01234567"
punctuation = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"
printable = digits + ascii_letters + punctuation + " \t\n\r\x0b\x0c"
whitespace = " \t\n\r\x0b\x0c"


def capwords(s: str, sep=None) -> str:
    return s


class Template:
    delimiter = "$"
    idpattern = r"(?a:[_a-z][_a-z0-9]*)"
    braceidpattern = None
    flags = 0

    def __init__(self, template: str):
        self.template = template

    def substitute(self, mapping=None, /, **kws):
        return self.template

    def safe_substitute(self, mapping=None, /, **kws):
        return self.template

    def is_valid(self):
        return nondet_bool()

    def get_identifiers(self):
        return nondet_list(8, nondet_str())


class Formatter:
    def format(self, format_string, /, *args, **kwargs):
        return format_string

    def vformat(self, format_string, args, kwargs):
        return format_string

    def parse(self, format_string):
        return nondet_list(8, nondet_str())

    def get_field(self, field_name, args, kwargs):
        return (None, field_name)

    def get_value(self, key, args, kwargs):
        return None

    def check_unused_args(self, used_args, args, kwargs):
        return None

    def format_field(self, value, format_spec):
        return str(value)

    def convert_field(self, value, conversion):
        return value
