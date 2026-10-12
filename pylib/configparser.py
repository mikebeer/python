# Pure Python configparser (common API of ConfigParser / RawConfigParser)
import re as _re
from collections import OrderedDict as _OrderedDict

__all__ = ["NoSectionError", "DuplicateOptionError", "DuplicateSectionError", "NoOptionError",
           "InterpolationError", "InterpolationDepthError", "InterpolationMissingOptionError",
           "InterpolationSyntaxError", "ParsingError", "MissingSectionHeaderError", "ConfigParser",
           "SafeConfigParser", "RawConfigParser", "Interpolation", "BasicInterpolation",
           "ExtendedInterpolation", "LegacyInterpolation", "SectionProxy", "ConverterMapping", "DEFAULTSECT",
           "MAX_INTERPOLATION_DEPTH", "UNNAMED_SECTION"]

DEFAULTSECT = "DEFAULT"
MAX_INTERPOLATION_DEPTH = 10
UNNAMED_SECTION = None


class Error(Exception):
    def __init__(self, msg=''):
        self.message = msg
        Exception.__init__(self, msg)

    def __repr__(self):
        return self.message

    __str__ = __repr__


class NoSectionError(Error):
    def __init__(self, section):
        Error.__init__(self, 'No section: %r' % (section,))
        self.section = section
        self.args = (section, )


class DuplicateSectionError(Error):
    def __init__(self, section, source=None, lineno=None):
        msg = [repr(section), " already exists"]
        if source is not None:
            message = ["While reading from ", repr(source)]
            if lineno is not None:
                message.append(" [line {0:2d}]".format(lineno))
            message.append(": section ")
            message.extend(msg)
            msg = message
        else:
            msg.insert(0, "Section ")
        Error.__init__(self, "".join(msg))
        self.section = section
        self.source = source
        self.lineno = lineno
        self.args = (section, source, lineno)


class DuplicateOptionError(Error):
    def __init__(self, section, option, source=None, lineno=None):
        msg = [repr(option), " in section ", repr(section), " already exists"]
        if source is not None:
            message = ["While reading from ", repr(source)]
            if lineno is not None:
                message.append(" [line {0:2d}]".format(lineno))
            message.append(": option ")
            message.extend(msg)
            msg = message
        else:
            msg.insert(0, "Option ")
        Error.__init__(self, "".join(msg))
        self.section = section
        self.option = option
        self.source = source
        self.lineno = lineno
        self.args = (section, option, source, lineno)


class NoOptionError(Error):
    def __init__(self, option, section):
        Error.__init__(self, "No option %r in section: %r" % (option, section))
        self.option = option
        self.section = section
        self.args = (option, section)


class InterpolationError(Error):
    def __init__(self, option, section, msg):
        Error.__init__(self, msg)
        self.option = option
        self.section = section
        self.args = (option, section, msg)


class InterpolationMissingOptionError(InterpolationError):
    def __init__(self, option, section, rawval, reference):
        msg = ("Bad value substitution: option {!r} in section {!r} contains an interpolation key {!r} which is not a valid option name. Raw value: {!r}".format(option, section, reference, rawval))
        InterpolationError.__init__(self, option, section, msg)
        self.reference = reference
        self.args = (option, section, rawval, reference)


class InterpolationSyntaxError(InterpolationError):
    pass


class InterpolationDepthError(InterpolationError):
    def __init__(self, option, section, rawval):
        msg = ("Recursion limit exceeded in value substitution: option {!r} in section {!r} contains an interpolation key which cannot be substituted in {} steps. Raw value: {!r}".format(option, section, MAX_INTERPOLATION_DEPTH, rawval))
        InterpolationError.__init__(self, option, section, msg)
        self.args = (option, section, rawval)


class ParsingError(Error):
    def __init__(self, source=None, filename=None):
        if filename and source:
            raise ValueError("Cannot specify both `filename' and `source'. Use `source'.")
        elif not filename and not source:
            raise ValueError("Required argument `source' not given.")
        elif filename:
            source = filename
        Error.__init__(self, 'Source contains parsing errors: %r' % source)
        self.source = source
        self.errors = []
        self.args = (source, )

    @property
    def filename(self):
        return self.source

    def append(self, lineno, line):
        self.errors.append((lineno, line))
        self.message += '\n\t[line %2d]: %s' % (lineno, line)


class MissingSectionHeaderError(ParsingError):
    def __init__(self, filename, lineno, line):
        Error.__init__(self, 'File contains no section headers.\nfile: %r, line: %d\n%r' % (filename, lineno, line))
        self.source = filename
        self.lineno = lineno
        self.line = line
        self.args = (filename, lineno, line)


class Interpolation:
    def before_get(self, parser, section, option, value, defaults):
        return value

    def before_set(self, parser, section, option, value):
        return value

    def before_read(self, parser, section, option, value):
        return value

    def before_write(self, parser, section, option, value):
        return value


class BasicInterpolation(Interpolation):
    _KEYCRE = _re.compile(r"%\(([^)]+)\)s")

    def before_get(self, parser, section, option, value, defaults):
        L = []
        self._interpolate_some(parser, option, L, value, section, defaults, 1)
        return ''.join(L)

    def before_set(self, parser, section, option, value):
        tmp_value = value.replace('%%', '')
        tmp_value = self._KEYCRE.sub('', tmp_value)
        if '%' in tmp_value:
            raise ValueError("invalid interpolation syntax in %r at position %d" % (value, tmp_value.find('%')))
        return value

    def _interpolate_some(self, parser, option, accum, rest, section, map, depth):
        rawval = parser.get(section, option, raw=True, fallback=rest)
        if depth > MAX_INTERPOLATION_DEPTH:
            raise InterpolationDepthError(option, section, rawval)
        while rest:
            p = rest.find("%")
            if p < 0:
                accum.append(rest)
                return
            if p > 0:
                accum.append(rest[:p])
                rest = rest[p:]
            c = rest[1:2]
            if c == "%":
                accum.append("%")
                rest = rest[2:]
            elif c == "(":
                m = self._KEYCRE.match(rest)
                if m is None:
                    raise InterpolationSyntaxError(option, section, "bad interpolation variable reference %r" % rest)
                var = parser.optionxform(m.group(1))
                rest = rest[m.end():]
                try:
                    v = map[var]
                except KeyError:
                    raise InterpolationMissingOptionError(option, section, rawval, var)
                if "%" in v:
                    self._interpolate_some(parser, option, accum, v, section, map, depth + 1)
                else:
                    accum.append(v)
            else:
                raise InterpolationSyntaxError(option, section, "'%%' must be followed by '%%' or '(', found: %r" % (rest,))


class ExtendedInterpolation(Interpolation):
    _KEYCRE = _re.compile(r"\$\{([^}]+)\}")

    def before_get(self, parser, section, option, value, defaults):
        L = []
        self._interpolate_some(parser, option, L, value, section, defaults, 1)
        return ''.join(L)

    def before_set(self, parser, section, option, value):
        tmp_value = value.replace('$$', '')
        tmp_value = self._KEYCRE.sub('', tmp_value)
        if '$' in tmp_value:
            raise ValueError("invalid interpolation syntax in %r at position %d" % (value, tmp_value.find('$')))
        return value

    def _interpolate_some(self, parser, option, accum, rest, section, map, depth):
        rawval = parser.get(section, option, raw=True, fallback=rest)
        if depth > MAX_INTERPOLATION_DEPTH:
            raise InterpolationDepthError(option, section, rawval)
        while rest:
            p = rest.find("$")
            if p < 0:
                accum.append(rest)
                return
            if p > 0:
                accum.append(rest[:p])
                rest = rest[p:]
            c = rest[1:2]
            if c == "$":
                accum.append("$")
                rest = rest[2:]
            elif c == "{":
                m = self._KEYCRE.match(rest)
                if m is None:
                    raise InterpolationSyntaxError(option, section, "bad interpolation variable reference %r" % rest)
                path = m.group(1).split(':')
                rest = rest[m.end():]
                sect = section
                opt = option
                try:
                    if len(path) == 1:
                        opt = parser.optionxform(path[0])
                        v = map[opt]
                    elif len(path) == 2:
                        sect = path[0]
                        opt = parser.optionxform(path[1])
                        v = parser.get(sect, opt, raw=True)
                    else:
                        raise InterpolationSyntaxError(option, section, "More than one ':' found: %r" % (rest,))
                except (KeyError, NoSectionError, NoOptionError):
                    raise InterpolationMissingOptionError(option, section, rawval, ":".join(path))
                if "$" in v:
                    self._interpolate_some(parser, opt, accum, v, sect, dict(parser.items(sect, raw=True)), depth + 1)
                else:
                    accum.append(v)
            else:
                raise InterpolationSyntaxError(option, section, "'$' must be followed by '$' or '{', found: %r" % (rest,))


class LegacyInterpolation(Interpolation):
    pass


class _Unset:
    def __repr__(self):
        return '<UNSET>'


_UNSET = _Unset()
_BOOL = {'1': True, 'yes': True, 'true': True, 'on': True, '0': False, 'no': False, 'false': False, 'off': False}


class RawConfigParser:
    _SECT_TMPL = r"\[(?P<header>.+)\]"
    BOOLEAN_STATES = _BOOL
    _DEFAULT_INTERPOLATION = Interpolation()

    def __init__(self, defaults=None, dict_type=dict, allow_no_value=False, *, delimiters=('=', ':'),
                 comment_prefixes=('#', ';'), inline_comment_prefixes=None, strict=True, empty_lines_in_values=True,
                 default_section=DEFAULTSECT, interpolation=_UNSET, converters=_UNSET, allow_unnamed_section=False):
        self._dict = dict_type
        self._sections = self._dict()
        self._defaults = self._dict()
        self._converters = ConverterMapping(self)
        self._proxies = self._dict()
        self._proxies[default_section] = SectionProxy(self, default_section)
        self._delimiters = tuple(delimiters)
        self._comment_prefixes = tuple(comment_prefixes or ())
        self._inline_comment_prefixes = tuple(inline_comment_prefixes or ())
        self._strict = strict
        self._allow_no_value = allow_no_value
        self._empty_lines_in_values = empty_lines_in_values
        self.default_section = default_section
        self._allow_unnamed_section = allow_unnamed_section
        if interpolation is _UNSET:
            self._interpolation = self._DEFAULT_INTERPOLATION
        else:
            self._interpolation = interpolation
        if self._interpolation is None:
            self._interpolation = Interpolation()
        if converters is not _UNSET:
            self._converters.update(converters)
        if defaults:
            self._read_defaults(defaults)

    def defaults(self):
        return self._defaults

    def sections(self):
        return list(self._sections.keys())

    def add_section(self, section):
        if section == self.default_section:
            raise ValueError('Invalid section name: %r' % section)
        if section in self._sections:
            raise DuplicateSectionError(section)
        self._sections[section] = self._dict()
        self._proxies[section] = SectionProxy(self, section)

    def has_section(self, section):
        return section in self._sections

    def options(self, section):
        try:
            opts = self._sections[section].copy()
        except KeyError:
            raise NoSectionError(section)
        opts.update(self._defaults)
        return list(opts.keys())

    def read(self, filenames, encoding=None):
        if isinstance(filenames, str):
            filenames = [filenames]
        read_ok = []
        for filename in filenames:
            try:
                fp = open(filename)
            except OSError:
                continue
            try:
                self._read(fp, filename)
            finally:
                fp.close()
            read_ok.append(filename)
        return read_ok

    def read_file(self, f, source=None):
        if source is None:
            try:
                source = f.name
            except AttributeError:
                source = '<???>'
        self._read(f, source)

    def read_string(self, string, source='<string>'):
        import io
        self.read_file(io.StringIO(string), source)

    def read_dict(self, dictionary, source='<dict>'):
        elements_added = set()
        for section, keys in dictionary.items():
            section = str(section)
            try:
                self.add_section(section)
            except (DuplicateSectionError, ValueError):
                if self._strict and section in elements_added:
                    raise
            elements_added.add(section)
            for key, value in keys.items():
                key = self.optionxform(str(key))
                if value is not None:
                    value = str(value)
                if self._strict and (section, key) in elements_added:
                    raise DuplicateOptionError(section, key, source)
                elements_added.add((section, key))
                self.set(section, key, value)

    def readfp(self, fp, filename=None):
        self.read_file(fp, source=filename)

    def get(self, section, option, *, raw=False, vars=None, fallback=_UNSET):
        try:
            d = self._unify_values(section, vars)
        except NoSectionError:
            if fallback is _UNSET:
                raise
            return fallback
        option = self.optionxform(option)
        try:
            value = d[option]
        except KeyError:
            if fallback is _UNSET:
                raise NoOptionError(option, section)
            return fallback
        if raw or value is None:
            return value
        return self._interpolation.before_get(self, section, option, value, d)

    def _get(self, section, conv, option, **kwargs):
        return conv(self.get(section, option, **kwargs))

    def _get_conv(self, section, option, conv, *, raw=False, vars=None, fallback=_UNSET, **kwargs):
        try:
            return self._get(section, conv, option, raw=raw, vars=vars, **kwargs)
        except (NoSectionError, NoOptionError):
            if fallback is _UNSET:
                raise
            return fallback

    def getint(self, section, option, *, raw=False, vars=None, fallback=_UNSET, **kwargs):
        return self._get_conv(section, option, int, raw=raw, vars=vars, fallback=fallback, **kwargs)

    def getfloat(self, section, option, *, raw=False, vars=None, fallback=_UNSET, **kwargs):
        return self._get_conv(section, option, float, raw=raw, vars=vars, fallback=fallback, **kwargs)

    def getboolean(self, section, option, *, raw=False, vars=None, fallback=_UNSET, **kwargs):
        return self._get_conv(section, option, self._convert_to_boolean, raw=raw, vars=vars, fallback=fallback, **kwargs)

    def items(self, section=_UNSET, raw=False, vars=None):
        if section is _UNSET:
            return [(sn, self[sn]) for sn in self.sections() + [self.default_section]]
        d = self._defaults.copy()
        try:
            d.update(self._sections[section])
        except KeyError:
            if section != self.default_section:
                raise NoSectionError(section)
        orig_keys = list(d.keys())
        if vars:
            for key, value in vars.items():
                d[self.optionxform(key)] = value
        value_getter = lambda option: self._interpolation.before_get(self, section, option, d[option], d)
        if raw:
            value_getter = lambda option: d[option]
        return [(option, value_getter(option)) for option in orig_keys]

    def popitem(self):
        for key in self.sections():
            value = self[key]
            del self[key]
            return key, value
        raise KeyError

    def optionxform(self, optionstr):
        return optionstr.lower()

    def has_option(self, section, option):
        if not section or section == self.default_section:
            option = self.optionxform(option)
            return option in self._defaults
        elif section not in self._sections:
            return False
        else:
            option = self.optionxform(option)
            return (option in self._sections[section] or option in self._defaults)

    def set(self, section, option, value=None):
        if value:
            value = self._interpolation.before_set(self, section, option, value)
        if not section or section == self.default_section:
            sectdict = self._defaults
        else:
            try:
                sectdict = self._sections[section]
            except KeyError:
                raise NoSectionError(section)
        sectdict[self.optionxform(option)] = value

    def write(self, fp, space_around_delimiters=True):
        if space_around_delimiters:
            d = " {} ".format(self._delimiters[0])
        else:
            d = self._delimiters[0]
        if self._defaults:
            self._write_section(fp, self.default_section, self._defaults.items(), d)
        for section in self._sections:
            self._write_section(fp, section, self._sections[section].items(), d)

    def _write_section(self, fp, section_name, section_items, delimiter):
        fp.write("[{}]\n".format(section_name))
        for key, value in section_items:
            value = self._interpolation.before_write(self, section_name, key, value)
            if value is not None or not self._allow_no_value:
                value = delimiter + str(value).replace('\n', '\n\t')
            else:
                value = ""
            fp.write("{}{}\n".format(key, value))
        fp.write("\n")

    def remove_option(self, section, option):
        if not section or section == self.default_section:
            sectdict = self._defaults
        else:
            try:
                sectdict = self._sections[section]
            except KeyError:
                raise NoSectionError(section)
        option = self.optionxform(option)
        existed = option in sectdict
        if existed:
            del sectdict[option]
        return existed

    def remove_section(self, section):
        existed = section in self._sections
        if existed:
            del self._sections[section]
            del self._proxies[section]
        return existed

    def __getitem__(self, key):
        if key != self.default_section and not self.has_section(key):
            raise KeyError(key)
        return self._proxies[key]

    def __setitem__(self, key, value):
        if key in self and self[key] is value:
            return
        if key == self.default_section:
            self._defaults.clear()
        elif key in self._sections:
            self._sections[key].clear()
        self.read_dict({key: value})

    def __delitem__(self, key):
        if key == self.default_section:
            raise ValueError("Cannot remove the default section.")
        if not self.has_section(key):
            raise KeyError(key)
        self.remove_section(key)

    def __contains__(self, key):
        return key == self.default_section or self.has_section(key)

    def __len__(self):
        return len(self._sections) + 1

    def __iter__(self):
        return iter([self.default_section] + list(self._sections.keys()))

    def _read(self, fp, fpname):
        elements_added = set()
        cursect = None
        sectname = None
        optname = None
        lineno = 0
        indent_level = 0
        e = None
        for lineno, line in enumerate(fp, start=1):
            comment_start = None
            value = line.strip()
            for prefix in self._inline_comment_prefixes:
                i = line.find(prefix)
                # inline comment must be preceded by whitespace
                while i > 0 and not line[i - 1].isspace():
                    i = line.find(prefix, i + 1)
                if i >= 0 and (comment_start is None or i < comment_start):
                    comment_start = i
            if comment_start is not None:
                value = line[:comment_start].strip()
            full_line_comment = False
            for prefix in self._comment_prefixes:
                if line.strip().startswith(prefix):
                    comment_start = 0
                    full_line_comment = True
                    value = ''
                    break
            if comment_start is None:
                value = line.strip()
            if not value:
                if self._empty_lines_in_values:
                    if (comment_start is None and cursect is not None and optname and cursect[optname] is not None):
                        cursect[optname].append('')
                else:
                    indent_level = sys_maxsize
                continue
            first_nonspace = len(line) - len(line.lstrip())
            cur_indent_level = first_nonspace
            if (cursect is not None and optname and cur_indent_level > indent_level):
                cursect[optname].append(value)
            else:
                indent_level = cur_indent_level
                if value.startswith('[') and value.endswith(']') and len(value) > 2:
                    sectname = value[1:-1]
                    if sectname in self._sections:
                        if self._strict and sectname in elements_added:
                            raise DuplicateSectionError(sectname, fpname, lineno)
                        cursect = self._sections[sectname]
                        elements_added.add(sectname)
                    elif sectname == self.default_section:
                        cursect = self._defaults
                    else:
                        cursect = self._dict()
                        self._sections[sectname] = cursect
                        self._proxies[sectname] = SectionProxy(self, sectname)
                        elements_added.add(sectname)
                    optname = None
                elif cursect is None:
                    raise MissingSectionHeaderError(fpname, lineno, line)
                else:
                    mo = self._match_option(value)
                    if mo is not None:
                        optname, vi, optval = mo
                        if not optname:
                            e = self._handle_error(e, fpname, lineno, line)
                        optname = self.optionxform(optname.rstrip())
                        if (self._strict and (sectname, optname) in elements_added):
                            raise DuplicateOptionError(sectname, optname, fpname, lineno)
                        elements_added.add((sectname, optname))
                        if optval is not None:
                            optval = optval.strip()
                            cursect[optname] = [optval]
                        else:
                            cursect[optname] = None
                    else:
                        e = self._handle_error(e, fpname, lineno, line)
        if e:
            raise e
        self._join_multiline_values()

    def _match_option(self, value):
        # find the earliest delimiter
        best = -1
        for d in self._delimiters:
            i = value.find(d)
            if i >= 0 and (best < 0 or i < best):
                best = i
        if best < 0:
            if self._allow_no_value:
                return (value, None, None)
            return None
        optname = value[:best]
        # Python's regex is lazy: `(?P<option>.*?)\s*(?P<vi>delims)\s*(?P<value>.*)$`
        return (optname.rstrip() if optname.strip() else '', value[best], value[best + 1:])

    def _handle_error(self, exc, fpname, lineno, line):
        if not exc:
            exc = ParsingError(fpname)
        exc.append(lineno, repr(line))
        return exc

    def _join_multiline_values(self):
        defaults = self.default_section, self._defaults
        all_sections = [defaults]
        for sn in self._sections:
            all_sections.append((sn, self._sections[sn]))
        for section, options in all_sections:
            for name, val in list(options.items()):
                if isinstance(val, list):
                    val = '\n'.join(val).rstrip()
                options[name] = self._interpolation.before_read(self, section, name, val)

    def _read_defaults(self, defaults):
        self.read_dict({self.default_section: defaults})

    def _unify_values(self, section, vars):
        sectiondict = {}
        try:
            sectiondict = self._sections[section]
        except KeyError:
            if section != self.default_section:
                raise NoSectionError(section)
        vardict = {}
        if vars:
            for key, value in vars.items():
                if value is not None:
                    value = str(value)
                vardict[self.optionxform(key)] = value
        m = {}
        m.update(self._defaults)
        m.update(sectiondict)
        m.update(vardict)
        return m

    def _convert_to_boolean(self, value):
        if value.lower() not in self.BOOLEAN_STATES:
            raise ValueError('Not a boolean: %s' % value)
        return self.BOOLEAN_STATES[value.lower()]

    def _validate_value_types(self, *, section="", option="", value=""):
        if not isinstance(section, str):
            raise TypeError("section names must be strings")
        if not isinstance(option, str):
            raise TypeError("option keys must be strings")
        if not self._allow_no_value or value:
            if not isinstance(value, str):
                raise TypeError("option values must be strings")

    @property
    def converters(self):
        return self._converters


sys_maxsize = 9223372036854775807


class ConfigParser(RawConfigParser):
    _DEFAULT_INTERPOLATION = BasicInterpolation()

    def set(self, section, option, value=None):
        self._validate_value_types(option=option, value=value)
        super().set(section, option, value)

    def add_section(self, section):
        self._validate_value_types(section=section)
        super().add_section(section)

    def _read_defaults(self, defaults):
        hold_interpolation = self._interpolation
        self._interpolation = Interpolation()
        try:
            self.read_dict({self.default_section: defaults})
        finally:
            self._interpolation = hold_interpolation


class SafeConfigParser(ConfigParser):
    pass


class SectionProxy:
    def __init__(self, parser, name):
        self._parser = parser
        self._name = name
        for conv in parser.converters:
            key = 'get' + conv
            getter = self._make_getter(conv)
            setattr(self, key, getter)

    def _make_getter(self, conv):
        def getter(option, fallback=None, *, raw=False, vars=None, **kwargs):
            return self.get(option, fallback, raw=raw, vars=vars, _impl=lambda section, opt, **kw: self._parser._get_conv(section, opt, self._parser.converters[conv], **kw), **kwargs)
        return getter

    def __repr__(self):
        return '<Section: {}>'.format(self._name)

    def __getitem__(self, key):
        if not self._parser.has_option(self._name, key):
            raise KeyError(key)
        return self._parser.get(self._name, key)

    def __setitem__(self, key, value):
        self._parser._validate_value_types(option=key, value=value)
        return self._parser.set(self._name, key, value)

    def __delitem__(self, key):
        if not (self._parser.has_option(self._name, key) and self._parser.remove_option(self._name, key)):
            raise KeyError(key)

    def __contains__(self, key):
        return self._parser.has_option(self._name, key)

    def __len__(self):
        return len(self._options())

    def __iter__(self):
        return iter(self._options())

    def _options(self):
        if self._name != self._parser.default_section:
            return self._parser.options(self._name)
        else:
            return self._parser.defaults()

    @property
    def parser(self):
        return self._parser

    @property
    def name(self):
        return self._name

    def get(self, option, fallback=None, *, raw=False, vars=None, _impl=None, **kwargs):
        if not _impl:
            _impl = self._parser.get
        return _impl(self._name, option, raw=raw, vars=vars, fallback=fallback, **kwargs)

    def getint(self, option, fallback=None, *, raw=False, vars=None):
        return self.get(option, fallback, raw=raw, vars=vars, _impl=self._parser.getint)

    def getfloat(self, option, fallback=None, *, raw=False, vars=None):
        return self.get(option, fallback, raw=raw, vars=vars, _impl=self._parser.getfloat)

    def getboolean(self, option, fallback=None, *, raw=False, vars=None):
        return self.get(option, fallback, raw=raw, vars=vars, _impl=self._parser.getboolean)

    def keys(self):
        return list(self._options())

    def values(self):
        return [self[k] for k in self._options()]

    def items(self):
        return [(k, self[k]) for k in self._options()]


class ConverterMapping:
    def __init__(self, parser):
        self._parser = parser
        self._data = {}

    def __getitem__(self, k):
        return self._data[k]

    def __setitem__(self, k, v):
        self._data[k] = v

    def __delitem__(self, k):
        del self._data[k]

    def __iter__(self):
        return iter(self._data)

    def __len__(self):
        return len(self._data)

    def update(self, other):
        for k in other:
            self._data[k] = other[k]
