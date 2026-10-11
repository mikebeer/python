/* =========================================================================
   p2_lex.c   tokens, character tables, lexer with INDENT/DEDENT
   ========================================================================= */
enum {
    TK_EOF = 1000, TK_NEWLINE, TK_INDENT, TK_DEDENT, TK_NAME, TK_NUM, TK_FLOAT, TK_STR,
    TK_FSTR, TK_KW, TK_EQ, TK_NE, TK_LE, TK_GE, TK_POW, TK_FDIV, TK_LSH, TK_RSH,
    TK_ARROW, TK_ELLIPSIS, TK_AUG
};
enum {
    B_NONE = 0, B_ADD, B_SUB, B_MUL, B_DIV, B_FDIV, B_MOD, B_POW, B_AND, B_OR, B_XOR,
    B_LSH, B_RSH
};
enum {
    KW_NONE = 0, KW_FALSE, KW_NONE_, KW_TRUE, KW_AND, KW_AS, KW_ASSERT, KW_BREAK, KW_CLASS,
    KW_CONTINUE, KW_DEF, KW_DEL, KW_ELIF, KW_ELSE, KW_EXCEPT, KW_FINALLY, KW_FOR, KW_FROM,
    KW_GLOBAL, KW_IF, KW_IMPORT, KW_IN, KW_IS, KW_LAMBDA, KW_NONLOCAL, KW_NOT, KW_OR,
    KW_PASS, KW_RAISE, KW_RETURN, KW_TRY, KW_WHILE, KW_WITH, KW_YIELD
};
static const char *kw_names[] = {
    "", "False", "None", "True", "and", "as", "assert", "break", "class", "continue", "def",
    "del", "elif", "else", "except", "finally", "for", "from", "global", "if", "import", "in",
    "is", "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try", "while",
    "with", "yield", NULL
};

typedef struct Tok {
    int type;
    int line;
    int kw;
    int op;
    int raw;
    double fval;
    Value val;
    Str *s;
} Tok;

static Tok *toks;
static int ntoks, tokcap;

static const char *CS_DIGIT = "0123456789";
static const char *CS_UP = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char *CS_LOW = "abcdefghijklmnopqrstuvwxyz";
static const char *CS_HEX = "0123456789abcdefABCDEF";

static void init_chars(void)
{
    const char *p;
    int i;
    for (i = 0; i < 256; i++) {
        ch_digit[i] = ch_alpha[i] = ch_alnum[i] = ch_space[i] = ch_upper[i] = ch_lower[i] = 0;
        ch_hex[i] = 0;
        to_up[i] = to_low[i] = (unsigned char)i;
    }
    for (p = CS_DIGIT; *p; p++) { ch_digit[(unsigned char)*p] = 1; ch_alnum[(unsigned char)*p] = 1; }
    for (p = CS_UP; *p; p++) {
        ch_upper[(unsigned char)*p] = 1; ch_alpha[(unsigned char)*p] = 1;
        ch_alnum[(unsigned char)*p] = 1;
    }
    for (p = CS_LOW; *p; p++) {
        ch_lower[(unsigned char)*p] = 1; ch_alpha[(unsigned char)*p] = 1;
        ch_alnum[(unsigned char)*p] = 1;
    }
    for (i = 0; CS_UP[i] && CS_LOW[i]; i++) {
        to_up[(unsigned char)CS_LOW[i]] = (unsigned char)CS_UP[i];
        to_low[(unsigned char)CS_UP[i]] = (unsigned char)CS_LOW[i];
    }
    for (p = CS_HEX; *p; p++) ch_hex[(unsigned char)*p] = 1;
    ch_alpha[(unsigned char)'_'] = 1;
    ch_alnum[(unsigned char)'_'] = 1;
    for (p = " \t\n\r\f\v"; *p; p++) ch_space[(unsigned char)*p] = 1;
}

static void syntax_error(int line, const char *fmt, ...)
{
    char buf[400];
    char buf2[460];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    if (strncmp(buf, "SyntaxError: ", 13) == 0) memmove(buf, buf + 13, strlen(buf) - 12);
    cur_line = line;
    sprintf(buf2, "%.380s (line %d)", buf, line);
    throw_val(new_exc(cls_SyntaxError, buf2));
}

static Tok *tok_new(int type, int line)
{
    Tok *t;
    if (ntoks >= tokcap) {
        tokcap = tokcap ? tokcap * 2 : 256;
        toks = (Tok *)xrealloc(toks, sizeof(Tok) * (size_t)tokcap);
    }
    t = &toks[ntoks++];
    t->type = type; t->line = line; t->kw = 0; t->op = 0; t->raw = 0;
    t->fval = 0; t->val = NONE_V; t->s = NULL;
    return t;
}

static void put_utf8(Buf *b, unsigned long c)
{
    if (c < 0x80UL) buf_addc(b, (int)c);
    else if (c < 0x800UL) {
        buf_addc(b, (int)(0xC0UL | (c >> 6)));
        buf_addc(b, (int)(0x80UL | (c & 0x3FUL)));
    } else if (c < 0x10000UL) {
        buf_addc(b, (int)(0xE0UL | (c >> 12)));
        buf_addc(b, (int)(0x80UL | ((c >> 6) & 0x3FUL)));
        buf_addc(b, (int)(0x80UL | (c & 0x3FUL)));
    } else {
        buf_addc(b, (int)(0xF0UL | (c >> 18)));
        buf_addc(b, (int)(0x80UL | ((c >> 12) & 0x3FUL)));
        buf_addc(b, (int)(0x80UL | ((c >> 6) & 0x3FUL)));
        buf_addc(b, (int)(0x80UL | (c & 0x3FUL)));
    }
}

/* process escapes of a string body into b */
static void unescape(const char *s, long n, Buf *b)
{
    long i = 0;
    while (i < n) {
        int c = s[i++];
        if (c != '\\' || i >= n) { buf_addc(b, c); continue; }
        c = s[i++];
        switch (c) {
        case 'n': buf_addc(b, '\n'); break;
        case 't': buf_addc(b, '\t'); break;
        case 'r': buf_addc(b, '\r'); break;
        case 'a': buf_addc(b, '\a'); break;
        case 'b': buf_addc(b, '\b'); break;
        case 'f': buf_addc(b, '\f'); break;
        case 'v': buf_addc(b, '\v'); break;
        case '\\': buf_addc(b, '\\'); break;
        case '\'': buf_addc(b, '\''); break;
        case '"': buf_addc(b, '"'); break;
        case '\n': break;
        case 'x': case 'u': case 'U': {
            int cnt = (c == 'x') ? 2 : (c == 'u' ? 4 : 8), k;
            unsigned long v = 0;
            for (k = 0; k < cnt && i < n && ch_hex[(unsigned char)s[i]]; k++, i++)
                v = v * 16 + (unsigned long)digit_val((unsigned char)s[i]);
            if (c == 'x') buf_addc(b, (int)v); else put_utf8(b, v);
            break; }
        default:
            if (c >= '0' && c <= '7') {
                unsigned long v = (unsigned long)(c - '0');
                int k;
                for (k = 0; k < 2 && i < n && s[i] >= '0' && s[i] <= '7'; k++, i++)
                    v = v * 8 + (unsigned long)(s[i] - '0');
                buf_addc(b, (int)v);
            } else {
                buf_addc(b, '\\');
                buf_addc(b, c);
            }
        }
    }
}

static int is_str_prefix(const char *s, int n)
{
    int i;
    if (n < 1 || n > 2) return 0;
    for (i = 0; i < n; i++)
        if (!strchr("rRbBuUfF", s[i])) return 0;
    return 1;
}

static void lex_source(const char *src, long len)
{
    long pos = 0;
    int line = 1, depth = 0, bol = 1;
    int stack[100], sp = 0;
    Tok *t;
    ntoks = 0;
    stack[0] = 0;
    while (pos < len) {
        int c;
        if (bol && depth == 0) {
            int col = 0;
            while (pos < len && (src[pos] == ' ' || src[pos] == '\t' || src[pos] == '\f')) {
                if (src[pos] == ' ') col++;
                else if (src[pos] == '\t') col = (col / 8 + 1) * 8;
                pos++;
            }
            if (pos >= len) break;
            if (src[pos] == '\n' || src[pos] == '\r' || src[pos] == '#') {
                while (pos < len && src[pos] != '\n') pos++;
                if (pos < len) { pos++; line++; }
                continue;
            }
            if (col > stack[sp]) {
                if (sp >= 98) syntax_error(line, "too many indentation levels");
                stack[++sp] = col;
                tok_new(TK_INDENT, line);
            } else {
                while (col < stack[sp]) { sp--; tok_new(TK_DEDENT, line); }
                if (col != stack[sp])
                    syntax_error(line, "unindent does not match any outer indentation level");
            }
            bol = 0;
        }
        c = (unsigned char)src[pos];
        if (c == '\n') {
            pos++;
            if (depth == 0) { tok_new(TK_NEWLINE, line); bol = 1; }
            line++;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f') { pos++; continue; }
        if (c == '#') { while (pos < len && src[pos] != '\n') pos++; continue; }
        if (c == '\\' && pos + 1 < len && (src[pos + 1] == '\n' || src[pos + 1] == '\r')) {
            pos++;
            if (src[pos] == '\r') pos++;
            if (pos < len && src[pos] == '\n') pos++;
            line++;
            continue;
        }
        if (ch_alpha[c]) {
            long st = pos;
            while (pos < len && ch_alnum[(unsigned char)src[pos]]) pos++;
            if (pos < len && (src[pos] == '"' || src[pos] == '\'') &&
                is_str_prefix(src + st, (int)(pos - st))) {
                /* string with prefix: fall into string scanning below */
                pos = st;
            } else {
                int k;
                t = tok_new(TK_NAME, line);
                t->s = intern(src + st, pos - st);
                for (k = 1; kw_names[k]; k++)
                    if (strlen(kw_names[k]) == (size_t)(pos - st) &&
                        memcmp(kw_names[k], src + st, (size_t)(pos - st)) == 0) {
                        t->type = TK_KW; t->kw = k; break;
                    }
                continue;
            }
        }
        if (ch_digit[c] || (c == '.' && pos + 1 < len && ch_digit[(unsigned char)src[pos + 1]])) {
            long st = pos;
            int isf = 0;
            char tmp[400];
            long n;
            if (c == '0' && pos + 1 < len && strchr("xXoObB", src[pos + 1])) {
                pos += 2;
                while (pos < len && (ch_hex[(unsigned char)src[pos]] || src[pos] == '_')) pos++;
            } else {
                while (pos < len && (ch_digit[(unsigned char)src[pos]] || src[pos] == '_')) pos++;
                if (pos < len && src[pos] == '.') {
                    isf = 1; pos++;
                    while (pos < len && (ch_digit[(unsigned char)src[pos]] || src[pos] == '_')) pos++;
                }
                if (pos < len && (src[pos] == 'e' || src[pos] == 'E')) {
                    long save = pos;
                    pos++;
                    if (pos < len && (src[pos] == '+' || src[pos] == '-')) pos++;
                    if (pos < len && ch_digit[(unsigned char)src[pos]]) {
                        isf = 1;
                        while (pos < len && ch_digit[(unsigned char)src[pos]]) pos++;
                    } else pos = save;
                }
            }
            n = pos - st;
            if (n > 390) syntax_error(line, "number too long");
            memcpy(tmp, src + st, (size_t)n);
            tmp[n] = 0;
            if (isf) {
                char tmp2[400];
                int k, j = 0;
                for (k = 0; tmp[k]; k++) if (tmp[k] != '_') tmp2[j++] = tmp[k];
                tmp2[j] = 0;
                t = tok_new(TK_FLOAT, line);
                t->fval = strtod(tmp2, NULL);
            } else {
                t = tok_new(TK_NUM, line);
                if (!parse_int(tmp, n, 0, &t->val)) syntax_error(line, "invalid number");
            }
            continue;
        }
        if (c == '"' || c == '\'' || ch_alpha[c]) {
            long st = pos;
            int fl = 0, rw = 0, q, triple = 0;
            long bs, be;
            while (pos < len && src[pos] != '"' && src[pos] != '\'') {
                if (src[pos] == 'f' || src[pos] == 'F') fl = 1;
                if (src[pos] == 'r' || src[pos] == 'R') rw = 1;
                pos++;
            }
            q = src[pos];
            if (pos + 2 < len && src[pos + 1] == q && src[pos + 2] == q) { triple = 1; pos += 3; }
            else pos++;
            bs = pos;
            for (;;) {
                if (pos >= len) syntax_error(line, "unterminated string literal");
                if (src[pos] == '\\' && pos + 1 < len) {
                    if (src[pos + 1] == '\n') line++;
                    pos += 2;
                    continue;
                }
                if (src[pos] == q) {
                    if (!triple) break;
                    if (pos + 2 < len + 0 && src[pos + 1] == q && src[pos + 2] == q) break;
                    if (pos + 2 == len - 0 && 0) break;
                }
                if (src[pos] == '\n') {
                    if (!triple) syntax_error(line, "unterminated string literal");
                    line++;
                }
                pos++;
            }
            be = pos;
            pos += triple ? 3 : 1;
            (void)st;
            t = tok_new(fl ? TK_FSTR : TK_STR, line);
            t->raw = rw;
            if (fl) {
                t->s = str_new(src + bs, be - bs);
            } else if (rw) {
                t->s = str_new(src + bs, be - bs);
            } else {
                Buf b;
                buf_init(&b);
                unescape(src + bs, be - bs, &b);
                t->s = str_new(b.s, b.n);
                buf_free(&b);
            }
            continue;
        }
        /* operators */
        {
            static const char *three[] = { "**=", "//=", ">>=", "<<=", "...", NULL };
            static const int tbop[] = { B_POW, B_FDIV, B_RSH, B_LSH, 0 };
            static const char *two[] = { "==", "!=", "<=", ">=", "**", "//", "<<", ">>", "->",
                "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "@=", ":=", NULL };
            static const int ttype[] = { TK_EQ, TK_NE, TK_LE, TK_GE, TK_POW, TK_FDIV, TK_LSH,
                TK_RSH, TK_ARROW, TK_AUG, TK_AUG, TK_AUG, TK_AUG, TK_AUG, TK_AUG, TK_AUG,
                TK_AUG, TK_AUG, ':' };
            static const int tbop2[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, B_ADD, B_SUB, B_MUL, B_DIV,
                B_MOD, B_AND, B_OR, B_XOR, B_MUL, 0 };
            int k, found = 0;
            for (k = 0; three[k]; k++)
                if (pos + 3 <= len && memcmp(src + pos, three[k], 3) == 0) {
                    if (k == 4) t = tok_new(TK_ELLIPSIS, line);
                    else { t = tok_new(TK_AUG, line); t->op = tbop[k]; }
                    pos += 3; found = 1; break;
                }
            if (found) continue;
            for (k = 0; two[k]; k++)
                if (pos + 2 <= len && memcmp(src + pos, two[k], 2) == 0) {
                    t = tok_new(ttype[k], line);
                    t->op = tbop2[k];
                    pos += 2; found = 1;
                    break;
                }
            if (found) continue;
        }
        if (strchr("([{", c)) depth++;
        else if (strchr(")]}", c)) { if (depth > 0) depth--; }
        if (!strchr("()[]{}+-*/%&|^~<>=,.:;@!", c)) syntax_error(line, "invalid character");
        tok_new(c, line);
        pos++;
    }
    if (ntoks > 0 && toks[ntoks - 1].type != TK_NEWLINE) tok_new(TK_NEWLINE, line);
    while (sp > 0) { sp--; tok_new(TK_DEDENT, line); }
    tok_new(TK_EOF, line);
}
