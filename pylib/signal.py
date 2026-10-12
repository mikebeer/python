# signal stub: handlers can be registered and queried but are never delivered
SIGHUP = 1
SIGINT = 2
SIGQUIT = 3
SIGABRT = 6
SIGKILL = 9
SIGUSR1 = 10
SIGSEGV = 11
SIGUSR2 = 12
SIGPIPE = 13
SIGALRM = 14
SIGTERM = 15
SIG_DFL = 0
SIG_IGN = 1
_handlers = {}


def default_int_handler(signum, frame):
    raise KeyboardInterrupt


def signal(signalnum, handler):
    old = _handlers.get(signalnum, SIG_DFL if signalnum != SIGINT else default_int_handler)
    _handlers[signalnum] = handler
    return old


def getsignal(signalnum):
    return _handlers.get(signalnum, SIG_DFL if signalnum != SIGINT else default_int_handler)


def alarm(seconds):
    return 0


def raise_signal(signalnum):
    h = getsignal(signalnum)
    if callable(h):
        h(signalnum, None)


def strsignal(signalnum):
    return {1: 'Hangup', 2: 'Interrupt', 3: 'Quit', 6: 'Aborted', 9: 'Killed', 15: 'Terminated'}.get(signalnum)
