# Pure Python html (escape/unescape)
__all__ = ['escape', 'unescape']


def escape(s, quote=True):
    s = s.replace("&", "&amp;")
    s = s.replace("<", "&lt;")
    s = s.replace(">", "&gt;")
    if quote:
        s = s.replace('"', "&quot;")
        s = s.replace('\'', "&#x27;")
    return s


_named = {'amp': '&', 'lt': '<', 'gt': '>', 'quot': '"', 'apos': "'", 'nbsp': '\u00a0', 'copy': '\u00a9', 'reg': '\u00ae',
          'hellip': '…', 'mdash': '—', 'ndash': '–', 'lsquo': '‘', 'rsquo': '’',
          'ldquo': '“', 'rdquo': '”', 'euro': '€', 'pound': '\u00a3', 'yen': '\u00a5', 'cent': '\u00a2',
          'deg': '\u00b0', 'plusmn': '\u00b1', 'times': '\u00d7', 'divide': '\u00f7', 'laquo': '\u00ab', 'raquo': '\u00bb',
          'middot': '\u00b7', 'bull': '•', 'trade': '™', 'sect': '\u00a7', 'para': '\u00b6', 'eacute': '\u00e9',
          'egrave': '\u00e8', 'agrave': '\u00e0', 'aacute': '\u00e1', 'uuml': '\u00fc', 'ouml': '\u00f6', 'auml': '\u00e4',
          'szlig': '\u00df', 'ntilde': '\u00f1', 'ccedil': '\u00e7', 'larr': '←', 'rarr': '→', 'hearts': '♥'}
_invalid_charrefs = {0x00: '�', 0x0d: '\r', 0x80: '€', 0x81: '\u0081', 0x82: '‚', 0x83: 'ƒ',
                     0x84: '„', 0x85: '…', 0x86: '†', 0x87: '‡', 0x88: 'ˆ', 0x89: '‰',
                     0x8a: 'Š', 0x8b: '‹', 0x8c: 'Œ', 0x8d: '\u008d', 0x8e: 'Ž', 0x8f: '\u008f',
                     0x90: '\u0090', 0x91: '‘', 0x92: '’', 0x93: '“', 0x94: '”', 0x95: '•',
                     0x96: '–', 0x97: '—', 0x98: '˜', 0x99: '™', 0x9a: 'š', 0x9b: '›',
                     0x9c: 'œ', 0x9d: '\u009d', 0x9e: 'ž', 0x9f: 'Ÿ'}


def unescape(s):
    if '&' not in s:
        return s
    out = []
    i = 0
    n = len(s)
    while i < n:
        c = s[i]
        if c != '&':
            out.append(c)
            i += 1
            continue
        if i + 1 < n and s[i + 1] == '#':
            j = i + 2
            hexa = False
            if j < n and s[j] in 'xX':
                hexa = True
                j += 1
            k = j
            digs = '0123456789abcdefABCDEF' if hexa else '0123456789'
            while k < n and s[k] in digs:
                k += 1
            if k == j:
                out.append(c)
                i += 1
                continue
            num = int(s[j:k], 16 if hexa else 10)
            if k < n and s[k] == ';':
                k += 1
            if num in _invalid_charrefs:
                out.append(_invalid_charrefs[num])
            elif 0xD800 <= num <= 0xDFFF or num > 0x10FFFF:
                out.append('�')
            elif (0x1 <= num <= 0x8) or (0xe <= num <= 0x1f) or (0x7f <= num <= 0x9f) or num == 0xb:
                pass
            else:
                out.append(chr(num))
            i = k
            continue
        j = i + 1
        while j < n and (s[j].isalnum()) and s[j].isascii():
            j += 1
        name = s[i + 1:j]
        if j < n and s[j] == ';' and name in _named:
            out.append(_named[name])
            i = j + 1
        else:
            # longest prefix without semicolon for the legacy entities
            done = False
            for e in range(len(name), 1, -1):
                if name[:e] in ('amp', 'lt', 'gt', 'quot', 'nbsp', 'copy', 'reg') and name[:e] in _named:
                    out.append(_named[name[:e]])
                    i = i + 1 + e
                    done = True
                    break
            if not done:
                out.append(c)
                i += 1
    return ''.join(out)
