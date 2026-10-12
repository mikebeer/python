import _pytime as T
import time
for s in [0, 86399, 951782400, 1700000000, 1709164800, -86400*400, 4102444800]:
    g = T.gmtime(s)
    print(tuple(g), T.strftime('%Y-%m-%d %H:%M:%S %a %A %b %B %j %U %W %V %G %u %w %y %I %p %e|%-d|%F %T %c', g), T.asctime(g), T.mktime(g))
print(repr(T.gmtime(1700000000)))
for f,s in [('%Y-%m-%d','2024-02-29'),('%d/%m/%y %H:%M','05/03/99 13:45'),('%b %d %Y %I:%M %p','Mar 5 2021 11:05 PM'),('%A, %B %d, %Y','Friday, July 04, 2025'),('%Y-%j','2024-366')]:
    print(tuple(T.strptime(s,f)))
try: T.strptime('x','%Y')
except ValueError as e: print(e)
try: T.strptime('2020 z','%Y')
except ValueError as e: print(e)
