/* =========================================================================
   p8_main.c   command line, REPL, main
   ========================================================================= */
static int line_opens_block(const char *s, long n)
{
    long i = 0, e = n;
    int q = 0;
    long last = -1;
    while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
    if (i < n && s[i] == '@') return 1;
    for (e = i; e < n; e++) {
        if (q) { if (s[e] == q) q = 0; continue; }
        if (s[e] == '"' || s[e] == '\'') { q = s[e]; continue; }
        if (s[e] == '#') break;
        if (s[e] != ' ' && s[e] != '\t' && s[e] != '\r' && s[e] != '\n') last = e;
    }
    return last >= 0 && s[last] == ':';
}
static int bracket_depth(const char *s, long n)
{
    long i;
    int d = 0, q = 0;
    for (i = 0; i < n; i++) {
        if (q) { if (s[i] == '\\') i++; else if (s[i] == q) q = 0; continue; }
        if (s[i] == '"' || s[i] == '\'') q = s[i];
        else if (s[i] == '#') { while (i < n && s[i] != '\n') i++; }
        else if (s[i] == '(' || s[i] == '[' || s[i] == '{') d++;
        else if (s[i] == ')' || s[i] == ']' || s[i] == '}') d--;
    }
    return d;
}

static void repl(int prompt)
{
    Buf acc;
    int c, block = 0;
    buf_init(&acc);
    script_name = "<stdin>";
    repl_echo = 1;
    for (;;) {
        Buf line;
        int blank;
        if (prompt) {
            fputs(acc.n == 0 ? ">>> " : "... ", stdout);
            fflush(stdout);
        }
        buf_init(&line);
        while ((c = getc(stdin)) != EOF && c != '\n') buf_addc(&line, c);
        if (c == EOF && line.n == 0) { buf_free(&line); break; }
        buf_addc(&line, '\n');
        blank = 1;
        {
            long i;
            for (i = 0; i < line.n; i++)
                if (line.s[i] != ' ' && line.s[i] != '\t' && line.s[i] != '\n' && line.s[i] != '\r')
                    blank = 0;
        }
        if (acc.n == 0 && blank) { buf_free(&line); continue; }
        if (acc.n == 0 && line_opens_block(line.s, line.n)) block = 1;
        buf_addn(&acc, line.s, line.n);
        buf_free(&line);
        if (block) {
            if (!blank) continue;
        } else if (bracket_depth(acc.s, acc.n) > 0) continue;
        run_source(acc.s, acc.n);
        acc.n = 0;
        acc.s[0] = 0;
        block = 0;
        fflush(stdout);
    }
    if (acc.n > 0) run_source(acc.s, acc.n);
    buf_free(&acc);
    if (prompt) fputc('\n', stdout);
}

int main(int argc, char **argv)
{
    const char *code = NULL, *file = NULL;
    int force_i = 0, i = 1, sargc = 0, rc = 0;
    char **sargv = NULL;
    static char dash_c[] = "-c";
    static char empty[] = "";
    char *dummy[2];
    while (i < argc && argv[i][0] == '-' && argv[i][1]) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            code = argv[i + 1];
            dummy[0] = dash_c;
            sargv = dummy;
            sargc = 1;
            i += 2;
            break;
        } else if (strcmp(argv[i], "-i") == 0) {
            force_i = 1;
            i++;
        } else break;
    }
    if (!code && i < argc) {
        file = argv[i];
        sargv = argv + i;
        sargc = argc - i;
    } else if (code) {
        /* sys.argv = ['-c', rest...] */
        sargv = (char **)xmalloc(sizeof(char *) * (size_t)(argc + 2));
        sargv[0] = dash_c;
        {
            int k;
            for (k = i; k < argc; k++) sargv[1 + k - i] = argv[k];
            sargc = 1 + argc - i;
        }
    } else {
        dummy[0] = empty;
        sargv = dummy;
        sargc = 1;
    }
    exe_path = argv[0];
    init_runtime(sargc, sargv);
    if (code) {
        script_name = "<string>";
        rc = run_source(code, (long)strlen(code));
    } else if (file) {
        long len;
        char *src = read_whole(file, &len);
        if (!src) {
            fprintf(stderr, "python: can't open file '%s'\n", file);
            return 2;
        }
        script_name = file;
        rc = run_source(src, len);
    } else {
        int prompt = force_i;
        if (host_isatty_stdin()) prompt = 1;
        repl(prompt);
    }
    fflush(stdout);
    return rc ? 1 : 0;
}
