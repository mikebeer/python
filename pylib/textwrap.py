"""textwrap: wrap, fill, dedent, indent, shorten (no regular expressions)."""

_WS = " \t\n\r\x0b\x0c"


def _isletter(c):
    return c.isalpha() or c == "_"


def _isw(c):
    return c.isalnum() or c == "_"


def _split_word(w, out):
    # split one whitespace-free token the way textwrap's wordsep_re does (hyphens and em-dashes)
    n = len(w)
    i = 0
    while i < n:
        if w[i] == "-":
            k = i
            while k < n and w[k] == "-":
                k += 1
            if k - i >= 2 and i > 0 and k < n and _isw(w[k]) and (_isw(w[i - 1]) or w[i - 1] in "!\"'&.,?"):
                out.append(w[i:k])
                i = k
                continue
        j = i + 1
        while True:
            if j >= n:
                out.append(w[i:])
                i = n
                break
            c = w[j]
            if c == "-" and j + 1 < n and _isletter(w[j + 1]):
                ok = False
                if j >= 2 and _isletter(w[j - 1]) and _isletter(w[j - 2]):
                    ok = True
                elif j >= 3 and _isletter(w[j - 1]) and w[j - 2] == "-" and _isletter(w[j - 3]):
                    ok = True
                if ok and j + 2 < n and (_isletter(w[j + 2]) or (w[j + 2] == "-" and j + 3 < n and
                                                                    _isletter(w[j + 3]))):
                    out.append(w[i:j + 1])
                    i = j + 1
                    break
            if c == "-" and j >= 1 and (_isw(w[j - 1]) or w[j - 1] in "!\"'&.,?"):
                k = j
                while k < n and w[k] == "-":
                    k += 1
                if k - j >= 2 and k < n and _isw(w[k]):
                    out.append(w[i:j])
                    i = j
                    break
            j += 1


def _split_chunks(text, break_on_hyphens):
    # split into alternating runs of whitespace / non-whitespace; words are further split after hyphens
    chunks = []
    i = 0
    n = len(text)
    while i < n:
        j = i
        if text[i] in _WS:
            while j < n and text[j] in _WS:
                j += 1
            chunks.append(text[i:j])
        else:
            while j < n and text[j] not in _WS:
                j += 1
            w = text[i:j]
            if break_on_hyphens and "-" in w:
                _split_word(w, chunks)
            else:
                chunks.append(w)
        i = j
    return chunks


def wrap(text, width=70, initial_indent="", subsequent_indent="", expand_tabs=True,
         replace_whitespace=True, fix_sentence_endings=False, break_long_words=True,
         drop_whitespace=True, break_on_hyphens=True, tabsize=8, max_lines=None, placeholder=" [...]"):
    if expand_tabs:
        text = text.expandtabs(tabsize)
    if replace_whitespace:
        for c in "\t\n\x0b\x0c\r":
            text = text.replace(c, " ")
    chunks = _split_chunks(text, break_on_hyphens)
    chunks.reverse()
    lines = []
    while chunks:
        cur = []
        cur_len = 0
        indent = subsequent_indent if lines else initial_indent
        w = width - len(indent)
        if drop_whitespace and chunks[-1].strip() == "" and lines:
            del chunks[-1]
        while chunks:
            ln = len(chunks[-1])
            if cur_len + ln <= w:
                cur.append(chunks.pop())
                cur_len += ln
            else:
                break
        if chunks and len(chunks[-1]) > w:
            if break_long_words:
                space_left = w - cur_len
                if w < 1:
                    space_left = 1
                ch = chunks[-1]
                cur.append(ch[:space_left])
                chunks[-1] = ch[space_left:]
                cur_len += space_left
            elif not cur:
                cur.append(chunks.pop())
                cur_len = len(cur[-1])
        if drop_whitespace and cur and cur[-1].strip() == "":
            cur_len -= len(cur[-1])
            del cur[-1]
        if cur:
            if max_lines is None or len(lines) + 1 < max_lines or \
                    (not chunks or drop_whitespace and len(chunks) == 1 and not chunks[0].strip()) \
                    and cur_len <= w:
                lines.append(indent + "".join(cur))
            else:
                while cur:
                    if cur[-1].strip() and cur_len + len(placeholder) <= w:
                        cur.append(placeholder)
                        lines.append(indent + "".join(cur))
                        break
                    cur_len -= len(cur[-1])
                    del cur[-1]
                else:
                    if lines:
                        prev = lines[-1].rstrip()
                        if len(prev) + len(placeholder) <= width:
                            lines[-1] = prev + placeholder
                            break
                    lines.append(indent + placeholder.lstrip())
                break
    return lines


def fill(text, width=70, **kw):
    return "\n".join(wrap(text, width, **kw))


def shorten(text, width, **kw):
    kw["max_lines"] = 1
    if "placeholder" not in kw:
        kw["placeholder"] = " [...]"
    return fill(" ".join(text.strip().split()), width, **kw)


def dedent(text):
    lines = text.split("\n")
    margin = None
    for ln in lines:
        s = ln.lstrip(" \t")
        if not s:
            continue
        ind = ln[:len(ln) - len(s)]
        if margin is None:
            margin = ind
        elif ind.startswith(margin):
            pass
        elif margin.startswith(ind):
            margin = ind
        else:
            k = 0
            while k < len(margin) and k < len(ind) and margin[k] == ind[k]:
                k += 1
            margin = margin[:k]
    out = []
    for ln in lines:
        if not ln.strip(" \t"):
            out.append("")
        elif margin:
            out.append(ln[len(margin):])
        else:
            out.append(ln)
    return "\n".join(out)


def indent(text, prefix, predicate=None):
    out = []
    for ln in text.splitlines(True):
        if predicate is None:
            ok = ln.strip() != ""
        else:
            ok = predicate(ln)
        out.append(prefix + ln if ok else ln)
    return "".join(out)
