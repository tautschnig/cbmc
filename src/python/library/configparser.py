"""
Verification model of the `configparser` module.

Nondet no-op configuration readers/writers. Every read returns
empty / nondet values; writes are no-ops.
"""


class Error(Exception):
    pass


class NoSectionError(Error):
    pass


class DuplicateSectionError(Error):
    pass


class NoOptionError(Error):
    pass


class InterpolationError(Error):
    pass


class InterpolationDepthError(InterpolationError):
    pass


class InterpolationMissingOptionError(InterpolationError):
    pass


class InterpolationSyntaxError(InterpolationError):
    pass


class ParsingError(Error):
    pass


class MissingSectionHeaderError(ParsingError):
    pass


class RawConfigParser:
    BOOLEAN_STATES = {"yes": True, "no": False, "true": True, "false": False}

    def __init__(self, defaults=None, dict_type=dict, allow_no_value=False,
                 *, delimiters=("=", ":"), comment_prefixes=("#", ";"),
                 inline_comment_prefixes=None, strict=True,
                 empty_lines_in_values=True, default_section="DEFAULT",
                 interpolation=None, converters=None):
        self._sections = {}
        self._defaults = defaults or {}

    def defaults(self):
        return self._defaults

    def sections(self):
        return list(self._sections.keys())

    def add_section(self, section):
        self._sections[section] = {}

    def has_section(self, section):
        return section in self._sections

    def options(self, section):
        return nondet_list(8, nondet_str())

    def read(self, filenames, encoding=None):
        return nondet_list(8, nondet_str())

    def read_file(self, f, source=None):
        return None

    def read_string(self, string, source="<string>"):
        return None

    def read_dict(self, dictionary, source="<dict>"):
        return None

    def get(self, section, option, *, raw=False, vars=None, fallback=None):
        return fallback if fallback is not None else nondet_str()

    def getint(self, section, option, *, raw=False, vars=None, fallback=None):
        return fallback if fallback is not None else nondet_int()

    def getfloat(self, section, option, *, raw=False, vars=None,
                 fallback=None):
        return fallback if fallback is not None else nondet_float()

    def getboolean(self, section, option, *, raw=False, vars=None,
                   fallback=None):
        return fallback if fallback is not None else nondet_bool()

    def has_option(self, section, option):
        return nondet_bool()

    def items(self, section=None, *, raw=False, vars=None):
        return nondet_list(8, nondet_str())

    def set(self, section, option, value=None):
        return None

    def write(self, fp, space_around_delimiters=True):
        return None

    def remove_option(self, section, option):
        return nondet_bool()

    def remove_section(self, section):
        return nondet_bool()

    def optionxform(self, optionstr):
        return optionstr.lower()


class ConfigParser(RawConfigParser):
    pass


class SafeConfigParser(ConfigParser):
    pass


DEFAULTSECT = "DEFAULT"


MAX_INTERPOLATION_DEPTH = 10


class BasicInterpolation:
    def before_get(self, parser, section, option, value, defaults):
        return value

    def before_set(self, parser, section, option, value):
        return value


class ExtendedInterpolation:
    def before_get(self, parser, section, option, value, defaults):
        return value

    def before_set(self, parser, section, option, value):
        return value
