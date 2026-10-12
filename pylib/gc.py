# gc stubs
enabled = True
garbage = []
callbacks = []
DEBUG_STATS = 1
DEBUG_COLLECTABLE = 2
DEBUG_UNCOLLECTABLE = 4
DEBUG_SAVEALL = 32
DEBUG_LEAK = 38
_threshold = (700, 10, 10)
_debug = 0


def enable():
    global enabled
    enabled = True


def disable():
    global enabled
    enabled = False


def isenabled():
    return enabled


def collect(generation=2):
    return 0


def get_count():
    return (0, 0, 0)


def get_threshold():
    return _threshold


def set_threshold(threshold0, threshold1=None, threshold2=None):
    global _threshold
    _threshold = (threshold0, threshold1 if threshold1 is not None else _threshold[1], threshold2 if threshold2 is not None else _threshold[2])


def get_debug():
    return _debug


def set_debug(flags):
    global _debug
    _debug = flags


def get_objects(generation=None):
    return []


def get_referrers(*objs):
    return []


def get_referents(*objs):
    return []


def get_stats():
    return [{'collections': 0, 'collected': 0, 'uncollectable': 0}] * 3


def is_tracked(obj):
    return not isinstance(obj, (int, float, str, bytes, bool, type(None)))


def is_finalized(obj):
    return False


def freeze():
    pass


def unfreeze():
    pass


def get_freeze_count():
    return 0
