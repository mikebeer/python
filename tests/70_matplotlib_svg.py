import io
import re
import matplotlib
matplotlib.use('Agg')
matplotlib.rcParams['svg.fonttype'] = 'none'
matplotlib.rcParams['figure.max_open_warning'] = 0
import matplotlib.colors
import matplotlib.pyplot as plt


def fl(seq):
    return [round(float(v), 9) + 0.0 for v in seq]


def show(name, fig, ax):
    fig.canvas.draw()
    print(name)
    print(' xlim', fl(ax.get_xlim()), 'ylim', fl(ax.get_ylim()))
    print(' xticks', fl(ax.get_xticks()))
    print(' yticks', fl(ax.get_yticks()))
    print(' xlabels', [t.get_text() for t in ax.get_xticklabels()], repr(ax.xaxis.get_offset_text().get_text()))
    print(' ylabels', [t.get_text() for t in ax.get_yticklabels()], repr(ax.yaxis.get_offset_text().get_text()))


def svg_of(fig):
    buf = io.StringIO()
    fig.savefig(buf, format='svg')
    return buf.getvalue()


def wellformed(s):
    stack = []
    for m in re.finditer(r'<(/?)([A-Za-z][\w:.-]*)([^<>]*?)(/?)>', s):
        close, tag, rest, selfc = m.groups()
        if selfc:
            continue
        if close:
            if not stack or stack.pop() != tag:
                return False
        else:
            stack.append(tag)
    return not stack and s.count('<svg') == 1 and s.rstrip().endswith('</svg>')


fig = plt.figure()
print(fl(fig.get_size_inches()), fig.dpi, fl(fig.get_size_inches() * fig.dpi))
fig = plt.figure(figsize=(8, 3), dpi=50)
print(fl(fig.get_size_inches()), fig.dpi)
plt.close('all')

# 1. basic line
fig, ax = plt.subplots()
l1, = ax.plot([1, 2, 3, 4], [1, 4, 2, 3], label='a')
l2, = ax.plot([0, 5], [0.5, 2.5], 'r--')
hx = matplotlib.colors.to_hex
print(hx(l1.get_color()), hx(l2.get_color()), l1.get_linestyle(), l2.get_linestyle(), l1.get_linewidth(), l1.get_marker())
print(len(ax.get_lines()), fl(l1.get_xdata()), fl(l1.get_ydata()))
show('line', fig, ax)

# 2. decimals and negatives
fig, ax = plt.subplots()
ax.plot([-1.5, -0.2, 0.7, 2.9], [0.001, 0.0042, 0.0031, 0.0075])
show('decimals', fig, ax)

# 3. large values (scientific order of magnitude)
fig, ax = plt.subplots()
ax.plot([0, 1, 2, 3], [1e6, 3.5e6, 2e6, 8e6])
show('large', fig, ax)

# 4. offset notation
fig, ax = plt.subplots()
ax.plot([100000, 100002, 100004, 100008], [1, 2, 3, 4])
show('offset', fig, ax)

# 5. tiny values
fig, ax = plt.subplots()
ax.plot([1e-6, 2e-6, 3e-6], [5e-9, 8e-9, 6e-9])
show('tiny', fig, ax)

# 6. bars (sticky zero) and categories
fig, ax = plt.subplots()
bc = ax.bar([1, 2, 3, 4], [5, 3, 8, 6])
print(len(ax.patches), [fl([p.get_x(), p.get_y(), p.get_width(), p.get_height()]) for p in ax.patches][:2])
show('bar', fig, ax)
fig, ax = plt.subplots()
ax.bar(['a', 'b', 'c'], [3, 7, 5])
show('catbar', fig, ax)
fig, ax = plt.subplots()
ax.barh(['x', 'y', 'z'], [3, 7, -5])
show('barh', fig, ax)

# 7. histogram
data = [1, 2, 2, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 6, 6, 7, 9]
fig, ax = plt.subplots()
n, edges, patches = ax.hist(data, bins=5)
print(fl(n), fl(edges), len(patches))
show('hist', fig, ax)
fig, ax = plt.subplots()
n, edges, patches = ax.hist(data, bins=[0, 2, 4, 10], density=True)
print(fl(n), fl(edges))
show('hist2', fig, ax)

# 8. scatter and mixed
fig, ax = plt.subplots()
ax.scatter([1, 2, 3, 4, 5], [10, 20, 15, 30, 25])
ax.plot([1, 5], [12, 28])
print(len(ax.collections), len(ax.get_lines()))
show('scatter', fig, ax)

# 9. subplots
fig, axs = plt.subplots(2, 2)
for i, a in enumerate(axs.flat):
    a.plot([0, 1, 2, 3], [i, i * 2 + 1, 3, 5 - i])
show('sub22', fig, axs[1, 0])
show('sub22b', fig, axs[0, 1])
fig, axs = plt.subplots(1, 3, figsize=(12, 3))
for a in axs:
    a.plot([0, 10], [0, 100])
show('sub13', fig, axs[2])

# 10. explicit limits and ticks
fig, ax = plt.subplots()
ax.plot([1, 2, 3], [4, 5, 6])
print(fl(ax.set_xlim(0, 10)), fl(ax.set_ylim(-1, 7)))
show('lims', fig, ax)
ax.set_xticks([0, 5, 10])
ax.set_yticks([0, 2, 4], ['lo', 'mid', 'hi'])
show('fixedticks', fig, ax)

# 11. degenerate data
fig, ax = plt.subplots()
ax.plot([3], [4])
show('single', fig, ax)
fig, ax = plt.subplots()
ax.plot([1, 2, 3], [7, 7, 7])
show('flat', fig, ax)
fig, ax = plt.subplots()
show('empty', fig, ax)

# 12. axhline, axvline, fill_between, errorbar
fig, ax = plt.subplots()
ax.plot([0, 10], [0, 5])
ax.axhline(8)
ax.axvline(-2)
show('axlines', fig, ax)
fig, ax = plt.subplots()
ax.fill_between([0, 1, 2, 3], [1, 3, 2, 4], [0, 1, 0, 1])
show('fill', fig, ax)
fig, ax = plt.subplots()
ax.errorbar([1, 2, 3], [2, 4, 3], yerr=[0.5, 1.0, 2.0])
show('errorbar', fig, ax)

# 13. pie
fig, ax = plt.subplots()
r = ax.pie([30, 20, 50], labels=['a', 'b', 'c'])
print(len(r[0]), fl(ax.get_xlim()), fl(ax.get_ylim()))

# 14. text / annotate do not move the limits; colour cycle
fig, ax = plt.subplots()
for k in range(3):
    ax.plot([0, 1], [k, k + 1])
ax.text(50, 50, 'hello')
ax.annotate('pt', xy=(1, 1), xytext=(20, 20))
print([hx(ln.get_color()) for ln in ax.get_lines()])
show('textlim', fig, ax)

# 15. titles and labels
fig, ax = plt.subplots()
ax.plot([1, 2], [3, 4])
ax.set_title('T')
ax.set_xlabel('xx')
ax.set_ylabel('yy')
print(ax.get_title(), ax.get_xlabel(), ax.get_ylabel())

# 16. a sweep of ranges
for a, b in [(0, 1), (0, 100), (-3, 3), (1000, 1010), (0.0001, 0.0009), (12345, 12399), (5, 5.001),
             (-1e-3, 1e-3), (0, 1e5), (0, 1e7), (0.5, 0.52), (-250, 1750), (2020, 2024), (0, 37), (1, 2.5e9)]:
    fig, ax = plt.subplots(figsize=(5, 3))
    ax.plot([a, b], [b, a])
    show('range %s %s' % (a, b), fig, ax)

# SVG checks
fig, ax = plt.subplots()
ax.plot([1, 2, 3, 4], [1, 4, 2, 3], label='one')
ax.plot([1, 2, 3, 4], [2, 3, 1, 4], label='two')
ax.scatter([1, 2, 3], [3, 3, 3], label='pts')
ax.set_title('Title')
ax.set_xlabel('X')
ax.set_ylabel('Y')
ax.legend()
ax.grid(True)
s = svg_of(fig)
print('svg', wellformed(s), s.startswith('<?xml'), '<svg' in s)
lo, hi = ax.get_xlim()
lo2, hi2 = ax.get_ylim()
ntick = sum(1 for t in ax.get_xticks() if lo <= t <= hi) + sum(1 for t in ax.get_yticks() if lo2 <= t <= hi2)
ntext = len(re.findall(r'<text[ >]', s))
print('texts', ntext == ntick + 3 + 3, 'Title' in s, '>one<' in s, '>pts<' in s)
print(wellformed('<svg><g></svg>'), wellformed('<svg><g/></svg>'))
plt.close('all')

# pandas .plot
import pandas as pd
sr = pd.Series([3, 1, 4, 1, 5, 9, 2, 6], name='v')
df = pd.DataFrame({'a': [1, 2, 3, 4], 'b': [4, 3, 1, 2]})
for name, mk in [('s.plot', lambda: sr.plot()), ('df.plot', lambda: df.plot()),
                 ('df.xy', lambda: df.plot(x='a', y='b')), ('df.bar', lambda: df.plot.bar()),
                 ('s.hist', lambda: sr.plot(kind='hist', bins=4)),
                 ('cat.bar', lambda: pd.Series([5, 3, 8], index=['x', 'y', 'z']).plot.bar()),
                 ('df.barh', lambda: df.plot.barh(stacked=True))]:
    ax = mk()
    show(name, ax.figure, ax)
    print(' lines', len(ax.get_lines()), 'patches', len(ax.patches), 'legend', ax.get_legend() is not None)
    s = svg_of(ax.figure)
    print(' svg', wellformed(s))
    plt.close('all')
