"""string constants and helpers."""
whitespace = " \t\n\r\x0b\x0c"
ascii_lowercase = "abcdefghijklmnopqrstuvwxyz"
ascii_uppercase = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
ascii_letters = ascii_lowercase + ascii_uppercase
digits = "0123456789"
hexdigits = "0123456789abcdefABCDEF"
octdigits = "01234567"
punctuation = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"
printable = digits + ascii_letters + punctuation + whitespace


def capwords(s, sep=None):
    return (sep or " ").join([w.capitalize() for w in s.split(sep)])


class Template:
    delimiter = "$"
    try:
        import re as _re
        pattern = _re.compile(r'\$(?:(?P<escaped>\$)|(?P<named>[_a-zA-Z][_a-zA-Z0-9]*)|\{(?P<braced>[_a-zA-Z][_a-zA-Z0-9]*)\}|(?P<invalid>))')
    except Exception:
        pattern = None

    def __init__(self, template):
        self.template = template

    def _sub(self, mapping, safe):
        t = self.template
        out = []
        i = 0
        n = len(t)
        idc = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"
        while i < n:
            c = t[i]
            if c != "$":
                out.append(c)
                i += 1
                continue
            if i + 1 < n and t[i + 1] == "$":
                out.append("$")
                i += 2
                continue
            if i + 1 < n and t[i + 1] == "{":
                j = t.find("}", i + 2)
                if j < 0:
                    if safe:
                        out.append(c)
                        i += 1
                        continue
                    raise ValueError("Invalid placeholder in string")
                name = t[i + 2:j]
                end = j + 1
            else:
                j = i + 1
                while j < n and t[j] in idc:
                    j += 1
                name = t[i + 1:j]
                end = j
                if not name or name[0] in "0123456789":
                    if safe:
                        out.append(c)
                        i += 1
                        continue
                    raise ValueError("Invalid placeholder in string")
            if name in mapping:
                out.append(str(mapping[name]))
            elif safe:
                out.append(t[i:end])
            else:
                raise KeyError(name)
            i = end
        return "".join(out)

    def substitute(self, mapping=None, **kw):
        m = dict(mapping) if mapping else {}
        m.update(kw)
        return self._sub(m, False)

    def safe_substitute(self, mapping=None, **kw):
        m = dict(mapping) if mapping else {}
        m.update(kw)
        return self._sub(m, True)


def _formatter_parser(fmt):
    # yields (literal_text, field_name, format_spec, conversion) like _string.formatter_parser
    i = 0
    n = len(fmt)
    lit = []
    while i < n:
        c = fmt[i]
        if c == '{':
            if i + 1 < n and fmt[i + 1] == '{':
                yield (''.join(lit) + '{', None, None, None)
                lit = []
                i += 2
                continue
            if i + 1 >= n:
                raise ValueError("Single '{' encountered in format string")
            depth = 1
            j = i + 1
            while j < n and depth:
                if fmt[j] == '{':
                    depth += 1
                elif fmt[j] == '}':
                    depth -= 1
                j += 1
            if depth:
                raise ValueError("expected '}' before end of string")
            body = fmt[i + 1:j - 1]
            conv = None
            spec = ''
            k = 0
            m = len(body)
            br = 0
            while k < m:
                ch = body[k]
                if ch == '[':
                    br += 1
                elif ch == ']':
                    br -= 1
                elif br == 0 and (ch == '!' or ch == ':'):
                    break
                k += 1
            name = body[:k]
            rest = body[k:]
            if rest.startswith('!'):
                if len(rest) < 2:
                    raise ValueError("end of string while looking for conversion specifier")
                conv = rest[1]
                rest = rest[2:]
                if rest and not rest.startswith(':'):
                    raise ValueError("expected ':' after conversion specifier")
            if rest.startswith(':'):
                spec = rest[1:]
            yield (''.join(lit), name, spec, conv)
            lit = []
            i = j
        elif c == '}':
            if i + 1 < n and fmt[i + 1] == '}':
                yield (''.join(lit) + '}', None, None, None)
                lit = []
                i += 2
                continue
            raise ValueError("Single '}' encountered in format string")
        else:
            lit.append(c)
            i += 1
    if lit:
        yield (''.join(lit), None, None, None)


class Formatter:
    def format(self, format_string, /, *args, **kwargs):
        return self.vformat(format_string, args, kwargs)

    def vformat(self, format_string, args, kwargs):
        used_args = set()
        result, _ = self._vformat(format_string, args, kwargs, used_args, 2)
        self.check_unused_args(used_args, args, kwargs)
        return result

    def _vformat(self, format_string, args, kwargs, used_args, recursion_depth, auto_arg_index=0):
        if recursion_depth < 0:
            raise ValueError('Max string recursion exceeded')
        result = []
        for literal_text, field_name, format_spec, conversion in self.parse(format_string):
            if literal_text:
                result.append(literal_text)
            if field_name is not None:
                if field_name == '':
                    if auto_arg_index is False:
                        raise ValueError('cannot switch from manual field specification to automatic field numbering')
                    field_name = str(auto_arg_index)
                    auto_arg_index += 1
                elif field_name.isdigit():
                    if auto_arg_index:
                        raise ValueError('cannot switch from manual field specification to automatic field numbering')
                    auto_arg_index = False
                obj, arg_used = self.get_field(field_name, args, kwargs)
                used_args.add(arg_used)
                obj = self.convert_field(obj, conversion)
                format_spec, auto_arg_index = self._vformat(format_spec, args, kwargs, used_args, recursion_depth - 1,
                                                            auto_arg_index=auto_arg_index)
                result.append(self.format_field(obj, format_spec))
        return ''.join(result), auto_arg_index

    def get_value(self, key, args, kwargs):
        if isinstance(key, int):
            return args[key]
        return kwargs[key]

    def check_unused_args(self, used_args, args, kwargs):
        pass

    def format_field(self, value, format_spec):
        return format(value, format_spec)

    def convert_field(self, value, conversion):
        if conversion is None:
            return value
        elif conversion == 's':
            return str(value)
        elif conversion == 'r':
            return repr(value)
        elif conversion == 'a':
            return ascii(value)
        raise ValueError("Unknown conversion specifier {0!s}".format(conversion))

    def parse(self, format_string):
        return _formatter_parser(format_string)

    def get_field(self, field_name, args, kwargs):
        i = 0
        n = len(field_name)
        while i < n and field_name[i] != '.' and field_name[i] != '[':
            i += 1
        first = field_name[:i]
        rest = field_name[i:]
        if first.isdigit():
            first = int(first)
        obj = self.get_value(first, args, kwargs)
        while rest:
            if rest[0] == '.':
                j = 1
                while j < len(rest) and rest[j] != '.' and rest[j] != '[':
                    j += 1
                obj = getattr(obj, rest[1:j])
                rest = rest[j:]
            else:
                j = rest.index(']')
                key = rest[1:j]
                if key.isdigit():
                    key = int(key)
                obj = obj[key]
                rest = rest[j + 1:]
        return obj, first
