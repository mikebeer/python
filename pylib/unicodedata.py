# unicodedata (minimal): category/name/numeric/decimal/digit/normalize for ASCII, Latin-1 and common ranges
unidata_version = '15.1.0'


def category(chr):
    o = ord(chr)
    if o < 128:
        if chr.isdigit():
            return 'Nd'
        if chr.isalpha():
            return 'Lu' if chr.isupper() else 'Ll'
        if o < 32 or o == 127:
            return 'Cc'
        if chr == ' ':
            return 'Zs'
        if chr in '_':
            return 'Pc'
        if chr in '-':
            return 'Pd'
        if chr in '([{':
            return 'Ps'
        if chr in ')]}':
            return 'Pe'
        if chr in '+<=>|~':
            return 'Sm'
        if chr in '$':
            return 'Sc'
        if chr in '^`':
            return 'Sk'
        return 'Po'
    if o < 160:
        return 'Cc'
    if o == 160:
        return 'Zs'
    if chr.isalpha():
        return 'Lu' if chr.isupper() else ('Ll' if chr.islower() else 'Lo')
    if chr.isdigit():
        return 'Nd'
    if chr.isspace():
        return 'Zs'
    return 'So'


def numeric(chr, *default):
    if chr.isdigit():
        return float(int(chr))
    if default:
        return default[0]
    raise ValueError('not a numeric character')


def decimal(chr, *default):
    if chr.isdigit():
        return int(chr)
    if default:
        return default[0]
    raise ValueError('not a decimal')


digit = decimal


def east_asian_width(chr):
    return 'Na' if ord(chr) < 128 else 'N'


def combining(chr):
    return 0


def bidirectional(chr):
    return 'L' if chr.isalpha() else ''


def mirrored(chr):
    return 1 if chr in '()<>[]{}' else 0


_DECOMP = {'à': 'à', 'á': 'á', 'â': 'â', 'ã': 'ã', 'ä': 'ä',
           'è': 'è', 'é': 'é', 'ê': 'ê', 'ë': 'ë',
           'ì': 'ì', 'í': 'í', 'î': 'î', 'ï': 'ï',
           'ò': 'ò', 'ó': 'ó', 'ô': 'ô', 'õ': 'õ', 'ö': 'ö',
           'ù': 'ù', 'ú': 'ú', 'û': 'û', 'ü': 'ü',
           'ñ': 'ñ', 'ç': 'ç',
           'À': 'À', 'Á': 'Á', 'Â': 'Â', 'Ã': 'Ã', 'Ä': 'Ä',
           'È': 'È', 'É': 'É', 'Ê': 'Ê', 'Ë': 'Ë',
           'Ñ': 'Ñ', 'Ö': 'Ö', 'Ü': 'Ü', 'Ç': 'Ç'}
_COMP = {}
for _k in _DECOMP:
    _COMP[_DECOMP[_k]] = _k
_COMPAT = {'ﬁ': 'fi', 'ﬂ': 'fl', '²': '2', '³': '3', '¹': '1', 'Ω': 'Ω',
           '½': '1⁄2', '™': 'TM', '…': '...', 'Ａ': 'A', ' ': ' '}


def normalize(form, unistr):
    if form not in ('NFC', 'NFD', 'NFKC', 'NFKD'):
        raise ValueError('invalid normalization form')
    out = unistr
    if form in ('NFKC', 'NFKD'):
        for k in _COMPAT:
            out = out.replace(k, _COMPAT[k])
    if form in ('NFD', 'NFKD'):
        for k in _DECOMP:
            out = out.replace(k, _DECOMP[k])
        return out
    for k in _COMP:
        out = out.replace(k, _COMP[k])
    return out


def is_normalized(form, unistr):
    return normalize(form, unistr) == unistr


_NAMES = {' ': 'SPACE', '!': 'EXCLAMATION MARK', '"': 'QUOTATION MARK', '#': 'NUMBER SIGN', '$': 'DOLLAR SIGN',
          '%': 'PERCENT SIGN', '&': 'AMPERSAND', "'": 'APOSTROPHE', '(': 'LEFT PARENTHESIS',
          ')': 'RIGHT PARENTHESIS', '*': 'ASTERISK', '+': 'PLUS SIGN', ',': 'COMMA', '-': 'HYPHEN-MINUS',
          '.': 'FULL STOP', '/': 'SOLIDUS', ':': 'COLON', ';': 'SEMICOLON', '<': 'LESS-THAN SIGN',
          '=': 'EQUALS SIGN', '>': 'GREATER-THAN SIGN', '?': 'QUESTION MARK', '@': 'COMMERCIAL AT',
          '[': 'LEFT SQUARE BRACKET', '\\': 'REVERSE SOLIDUS', ']': 'RIGHT SQUARE BRACKET',
          '^': 'CIRCUMFLEX ACCENT', '_': 'LOW LINE', '`': 'GRAVE ACCENT', '{': 'LEFT CURLY BRACKET',
          '|': 'VERTICAL LINE', '}': 'RIGHT CURLY BRACKET', '~': 'TILDE', 'é': 'LATIN SMALL LETTER E WITH ACUTE'}
_DIGITS = ['ZERO', 'ONE', 'TWO', 'THREE', 'FOUR', 'FIVE', 'SIX', 'SEVEN', 'EIGHT', 'NINE']


def name(chr, *default):
    if chr in _NAMES:
        return _NAMES[chr]
    if '0' <= chr <= '9':
        return 'DIGIT ' + _DIGITS[ord(chr) - 48]
    if 'A' <= chr <= 'Z':
        return 'LATIN CAPITAL LETTER ' + chr
    if 'a' <= chr <= 'z':
        return 'LATIN SMALL LETTER ' + chr.upper()
    if default:
        return default[0]
    raise ValueError('no such name')


def lookup(name_):
    for k in _NAMES:
        if _NAMES[k] == name_:
            return k
    for o in range(48, 58):
        if name_ == 'DIGIT ' + _DIGITS[o - 48]:
            return chr(o)
    if name_.startswith('LATIN CAPITAL LETTER ') and len(name_) == 22:
        return name_[-1]
    if name_.startswith('LATIN SMALL LETTER ') and len(name_) == 20:
        return name_[-1].lower()
    raise KeyError("undefined character name '%s'" % name_)
