# locale: the "C" locale only
LC_CTYPE = 0
LC_NUMERIC = 1
LC_TIME = 2
LC_COLLATE = 3
LC_MONETARY = 4
LC_MESSAGES = 5
LC_ALL = 6
CHAR_MAX = 127


class Error(Exception):
    pass


def setlocale(category, locale=None):
    if locale is None or locale in ('', 'C', 'POSIX'):
        return 'C'
    raise Error('unsupported locale setting')


def getlocale(category=LC_CTYPE):
    return (None, None)


def getdefaultlocale(envvars=('LC_ALL', 'LC_CTYPE', 'LANG', 'LANGUAGE')):
    return (None, None)


def getpreferredencoding(do_setlocale=True):
    return 'utf-8'


def getencoding():
    return 'utf-8'


def localeconv():
    return {'int_curr_symbol': '', 'currency_symbol': '', 'mon_decimal_point': '', 'mon_thousands_sep': '',
            'mon_grouping': [], 'positive_sign': '', 'negative_sign': '', 'int_frac_digits': 127,
            'frac_digits': 127, 'p_cs_precedes': 127, 'p_sep_by_space': 127, 'n_cs_precedes': 127,
            'n_sep_by_space': 127, 'p_sign_posn': 127, 'n_sign_posn': 127, 'decimal_point': '.',
            'thousands_sep': '', 'grouping': []}


def normalize(localename):
    return localename


def strcoll(a, b):
    return (a > b) - (a < b)


def strxfrm(s):
    return s


def format_string(f, val, grouping=False, monetary=False):
    return f % val


def format(percent, value, grouping=False, monetary=False, *additional):
    return percent % value


def currency(val, symbol=True, grouping=False, international=False):
    raise ValueError("Currency formatting is not possible using the 'C' locale.")


def str(val):
    return '%.12g' % val


def atof(string, func=float):
    return func(string)


def atoi(string):
    return int(string)


def delocalize(string):
    return string


def localize(string, grouping=False, monetary=False):
    return string
