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
