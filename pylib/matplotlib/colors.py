"""Colour helpers: tab10 cycle, named colours, hex conversion, a small viridis map."""
TAB10 = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728', '#9467bd',
         '#8c564b', '#e377c2', '#7f7f7f', '#bcbd22', '#17becf']
NAMED = {
    'b': '#0000ff', 'g': '#008000', 'r': '#ff0000', 'c': '#00bfbf', 'm': '#bf00bf',
    'y': '#bfbf00', 'k': '#000000', 'w': '#ffffff',
    'blue': '#0000ff', 'green': '#008000', 'red': '#ff0000', 'cyan': '#00ffff',
    'magenta': '#ff00ff', 'yellow': '#ffff00', 'black': '#000000', 'white': '#ffffff',
    'orange': '#ffa500', 'purple': '#800080', 'brown': '#a52a2a', 'pink': '#ffc0cb',
    'gray': '#808080', 'grey': '#808080', 'lightgray': '#d3d3d3', 'lightgrey': '#d3d3d3',
    'darkgray': '#a9a9a9', 'olive': '#808000', 'navy': '#000080', 'teal': '#008080',
    'gold': '#ffd700', 'salmon': '#fa8072', 'skyblue': '#87ceeb', 'lightblue': '#add8e6',
    'lightgreen': '#90ee90', 'darkgreen': '#006400', 'darkblue': '#00008b',
    'darkred': '#8b0000', 'crimson': '#dc143c', 'coral': '#ff7f50', 'tan': '#d2b48c',
    'violet': '#ee82ee', 'indigo': '#4b0082', 'maroon': '#800000', 'lime': '#00ff00',
    'steelblue': '#4682b4', 'tomato': '#ff6347', 'none': 'none',
}
for _i, _nm in enumerate(['blue', 'orange', 'green', 'red', 'purple', 'brown', 'pink', 'gray', 'olive', 'cyan']):
    NAMED['tab:' + _nm] = TAB10[_i]
_VIRIDIS = ['#440154', '#472d7b', '#3b528b', '#2c728e', '#21918c', '#28ae80',
            '#5ec962', '#addc30', '#fde725']


def to_hex(c):
    if c is None:
        return 'none'
    if isinstance(c, str):
        s = c
        if len(s) == 2 and s[0] == 'C' and s[1].isdigit():
            return TAB10[int(s[1])]
        if s.startswith('#'):
            if len(s) == 4:
                s = '#' + s[1] * 2 + s[2] * 2 + s[3] * 2
            return s.lower()
        if s in NAMED:
            return NAMED[s]
        if s.lower() in NAMED:
            return NAMED[s.lower()]
        try:
            v = float(s)
            g = int(round(v * 255))
            return '#%02x%02x%02x' % (g, g, g)
        except ValueError:
            raise ValueError('Invalid RGBA argument: %r' % (c,))
    vals = [float(v) for v in c]
    return '#%02x%02x%02x' % tuple(int(round(max(0.0, min(1.0, v)) * 255)) for v in vals[:3])


def to_rgb(c):
    h = to_hex(c)
    return (int(h[1:3], 16) / 255.0, int(h[3:5], 16) / 255.0, int(h[5:7], 16) / 255.0)


def is_color_like(c):
    try:
        to_hex(c)
        return True
    except Exception:
        return False


def cmap_hex(t):
    """Colour of the viridis map at 0 <= t <= 1."""
    t = max(0.0, min(1.0, t)) * (len(_VIRIDIS) - 1)
    i = min(int(t), len(_VIRIDIS) - 2)
    a = to_rgb(_VIRIDIS[i])
    b = to_rgb(_VIRIDIS[i + 1])
    f = t - i
    return to_hex(tuple(a[k] + (b[k] - a[k]) * f for k in range(3)))
