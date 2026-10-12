# Pure Python calendar
import sys as _sys
import datetime as _datetime

__all__ = ["IllegalMonthError", "IllegalWeekdayError", "setfirstweekday", "firstweekday", "isleap", "leapdays",
           "weekday", "monthrange", "monthcalendar", "prmonth", "month", "prcal", "calendar", "timegm",
           "month_name", "month_abbr", "day_name", "day_abbr", "Calendar", "TextCalendar", "HTMLCalendar",
           "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY", "SUNDAY", "Day", "Month",
           "JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY", "AUGUST", "SEPTEMBER", "OCTOBER",
           "NOVEMBER", "DECEMBER", "mdays", "error", "January", "February"]

error = ValueError


class IllegalMonthError(ValueError):
    def __init__(self, month):
        self.month = month

    def __str__(self):
        return "bad month number %r; must be 1-12" % self.month


class IllegalWeekdayError(ValueError):
    def __init__(self, weekday):
        self.weekday = weekday

    def __str__(self):
        return "bad weekday number %r; must be 0 (Monday) to 6 (Sunday)" % self.weekday


_DNAMES = ['MONDAY', 'TUESDAY', 'WEDNESDAY', 'THURSDAY', 'FRIDAY', 'SATURDAY', 'SUNDAY']
_MNAMES = ['', 'JANUARY', 'FEBRUARY', 'MARCH', 'APRIL', 'MAY', 'JUNE', 'JULY', 'AUGUST', 'SEPTEMBER', 'OCTOBER',
           'NOVEMBER', 'DECEMBER']


class Day(int):
    def __repr__(self):
        return 'calendar.' + _DNAMES[int(self)]

    def __str__(self):
        return str(int(self))

    @property
    def name(self):
        return _DNAMES[int(self)]

    @property
    def value(self):
        return int(self)


class Month(int):
    def __repr__(self):
        return 'calendar.' + _MNAMES[int(self)]

    def __str__(self):
        return str(int(self))

    @property
    def name(self):
        return _MNAMES[int(self)]

    @property
    def value(self):
        return int(self)


January = 1
February = 2
mdays = [0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
JANUARY, FEBRUARY, MARCH, APRIL, MAY, JUNE, JULY, AUGUST, SEPTEMBER, OCTOBER, NOVEMBER, DECEMBER = [Month(i) for i in range(1, 13)]
MONDAY, TUESDAY, WEDNESDAY, THURSDAY, FRIDAY, SATURDAY, SUNDAY = [Day(i) for i in range(7)]

_dnames = ['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday', 'Sunday']
_mnames = ['', 'January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October',
           'November', 'December']


class _NameList:
    def __init__(self, names, n):
        self._names = names
        self._n = n

    def __getitem__(self, i):
        if isinstance(i, slice):
            return [x[:self._n] if self._n else x for x in self._names[i]]
        x = self._names[i]
        return x[:self._n] if self._n else x

    def __len__(self):
        return len(self._names)

    def __iter__(self):
        return iter([self[i] for i in range(len(self._names))])


day_name = _NameList(_dnames, 0)
day_abbr = _NameList(_dnames, 3)
month_name = _NameList(_mnames, 0)
month_abbr = _NameList(_mnames, 3)


def isleap(year):
    return year % 4 == 0 and (year % 100 != 0 or year % 400 == 0)


def leapdays(y1, y2):
    y1 -= 1
    y2 -= 1
    return (y2 // 4 - y1 // 4) - (y2 // 100 - y1 // 100) + (y2 // 400 - y1 // 400)


def _days_before_year(year):
    y = year - 1
    return y * 365 + y // 4 - y // 100 + y // 400


def _ordinal(year, month, day):
    days = _days_before_year(year)
    for m in range(1, month):
        days += mdays[m] + (1 if m == 2 and isleap(year) else 0)
    return days + day


def weekday(year, month, day):
    if not 1 <= year <= 9999:
        year = 2000 + year % 400
    return Day((_ordinal(year, month, day) + 6) % 7)


def monthrange(year, month):
    if not 1 <= month <= 12:
        raise IllegalMonthError(month)
    day1 = weekday(year, month, 1)
    ndays = mdays[month] + (month == 2 and isleap(year))
    return day1, ndays


def _monthlen(year, month):
    return mdays[month] + (month == 2 and isleap(year))


def _prevmonth(year, month):
    if month == 1:
        return year - 1, 12
    return year, month - 1


def _nextmonth(year, month):
    if month == 12:
        return year + 1, 1
    return year, month + 1


class Calendar:
    def __init__(self, firstweekday=0):
        self.firstweekday = firstweekday

    def getfirstweekday(self):
        return self._firstweekday % 7

    def setfirstweekday(self, firstweekday):
        self._firstweekday = firstweekday

    firstweekday = property(getfirstweekday, setfirstweekday)

    def iterweekdays(self):
        for i in range(self.firstweekday, self.firstweekday + 7):
            yield i % 7

    def itermonthdates(self, year, month):
        for y, m, d in self.itermonthdays3(year, month):
            yield _datetime.date(y, m, d)

    def itermonthdays(self, year, month):
        day1, ndays = monthrange(year, month)
        days_before = (day1 - self.firstweekday) % 7
        yield from [0] * days_before
        yield from range(1, ndays + 1)
        days_after = (self.firstweekday - day1 - ndays) % 7
        yield from [0] * days_after

    def itermonthdays2(self, year, month):
        for i, d in enumerate(self.itermonthdays(year, month), self.firstweekday):
            yield d, i % 7

    def itermonthdays3(self, year, month):
        day1, ndays = monthrange(year, month)
        days_before = (day1 - self.firstweekday) % 7
        days_after = (self.firstweekday - day1 - ndays) % 7
        y, m = _prevmonth(year, month)
        end = _monthlen(y, m) + 1
        for d in range(end - days_before, end):
            yield y, m, d
        for d in range(1, ndays + 1):
            yield year, month, d
        y, m = _nextmonth(year, month)
        for d in range(1, days_after + 1):
            yield y, m, d

    def itermonthdays4(self, year, month):
        for i, (y, m, d) in enumerate(self.itermonthdays3(year, month)):
            yield y, m, d, (self.firstweekday + i) % 7

    def monthdatescalendar(self, year, month):
        dates = list(self.itermonthdates(year, month))
        return [dates[i:i + 7] for i in range(0, len(dates), 7)]

    def monthdays2calendar(self, year, month):
        days = list(self.itermonthdays2(year, month))
        return [days[i:i + 7] for i in range(0, len(days), 7)]

    def monthdayscalendar(self, year, month):
        days = list(self.itermonthdays(year, month))
        return [days[i:i + 7] for i in range(0, len(days), 7)]

    def yeardatescalendar(self, year, width=3):
        months = [self.monthdatescalendar(year, i) for i in range(1, 13)]
        return [months[i:i + width] for i in range(0, len(months), width)]

    def yeardays2calendar(self, year, width=3):
        months = [self.monthdays2calendar(year, i) for i in range(1, 13)]
        return [months[i:i + width] for i in range(0, len(months), width)]

    def yeardayscalendar(self, year, width=3):
        months = [self.monthdayscalendar(year, i) for i in range(1, 13)]
        return [months[i:i + width] for i in range(0, len(months), width)]


class TextCalendar(Calendar):
    def prweek(self, theweek, width):
        print(self.formatweek(theweek, width), end='')

    def formatday(self, day, weekday, width):
        if day == 0:
            s = ''
        else:
            s = '%2i' % day
        return s.center(width)

    def formatweek(self, theweek, width):
        return ' '.join(self.formatday(d, wd, width) for (d, wd) in theweek)

    def formatweekday(self, day, width):
        if width >= 9:
            names = day_name
        else:
            names = day_abbr
        return names[day][:width].center(width)

    def formatweekheader(self, width):
        return ' '.join(self.formatweekday(i, width) for i in self.iterweekdays())

    def formatmonthname(self, theyear, themonth, width, withyear=True):
        s = month_name[themonth]
        if withyear:
            s = "%s %r" % (s, theyear)
        return s.center(width)

    def prmonth(self, theyear, themonth, w=0, l=0):
        print(self.formatmonth(theyear, themonth, w, l), end='')

    def formatmonth(self, theyear, themonth, w=0, l=0):
        w = max(2, w)
        l = max(1, l)
        s = self.formatmonthname(theyear, themonth, 7 * (w + 1) - 1)
        s = s.rstrip()
        s += '\n' * l
        s += self.formatweekheader(w).rstrip()
        s += '\n' * l
        for week in self.monthdays2calendar(theyear, themonth):
            s += self.formatweek(week, w).rstrip()
            s += '\n' * l
        return s

    def formatyear(self, theyear, w=2, l=1, c=6, m=3):
        w = max(2, w)
        l = max(1, l)
        c = max(2, c)
        colwidth = (w + 1) * 7 - 1
        v = []
        a = v.append
        a(repr(theyear).center(colwidth * m + c * (m - 1)).rstrip())
        a('\n' * l)
        header = self.formatweekheader(w)
        for (i, row) in enumerate(self.yeardays2calendar(theyear, m)):
            months = range(m * i + 1, min(m * (i + 1) + 1, 13))
            a('\n' * l)
            names = (self.formatmonthname(theyear, k, colwidth, False) for k in months)
            a(formatstring(names, colwidth, c).rstrip())
            a('\n' * l)
            headers = (header for k in months)
            a(formatstring(headers, colwidth, c).rstrip())
            a('\n' * l)
            height = max(len(cal) for cal in row)
            for j in range(height):
                weeks = []
                for cal in row:
                    if j >= len(cal):
                        weeks.append('')
                    else:
                        weeks.append(self.formatweek(cal[j], w))
                a(formatstring(weeks, colwidth, c).rstrip())
                a('\n' * l)
        return ''.join(v)

    def pryear(self, theyear, w=0, l=0, c=6, m=3):
        print(self.formatyear(theyear, w, l, c, m), end='')


class HTMLCalendar(Calendar):
    cssclasses = ["mon", "tue", "wed", "thu", "fri", "sat", "sun"]
    cssclasses_weekday_head = cssclasses
    cssclass_noday = "noday"
    cssclass_month_head = "month"
    cssclass_month = "month"
    cssclass_year = "year"
    cssclass_year_head = "year"

    def formatday(self, day, weekday):
        if day == 0:
            return '<td class="%s">&nbsp;</td>' % self.cssclass_noday
        return '<td class="%s">%d</td>' % (self.cssclasses[weekday], day)

    def formatweek(self, theweek):
        s = ''.join(self.formatday(d, wd) for (d, wd) in theweek)
        return '<tr>%s</tr>' % s

    def formatweekday(self, day):
        return '<th class="%s">%s</th>' % (self.cssclasses_weekday_head[day], day_abbr[day])

    def formatweekheader(self):
        s = ''.join(self.formatweekday(i) for i in self.iterweekdays())
        return '<tr>%s</tr>' % s

    def formatmonthname(self, theyear, themonth, withyear=True):
        if withyear:
            s = '%s %s' % (month_name[themonth], theyear)
        else:
            s = '%s' % month_name[themonth]
        return '<tr><th colspan="7" class="%s">%s</th></tr>' % (self.cssclass_month_head, s)

    def formatmonth(self, theyear, themonth, withyear=True):
        v = []
        a = v.append
        a('<table border="0" cellpadding="0" cellspacing="0" class="%s">' % (self.cssclass_month))
        a('\n')
        a(self.formatmonthname(theyear, themonth, withyear=withyear))
        a('\n')
        a(self.formatweekheader())
        a('\n')
        for week in self.monthdays2calendar(theyear, themonth):
            a(self.formatweek(week))
            a('\n')
        a('</table>')
        a('\n')
        return ''.join(v)


def formatstring(cols, colwidth=20, spacing=6):
    spacing = ' ' * spacing
    return spacing.join(c.center(colwidth) for c in cols)


c = TextCalendar()


def setfirstweekday(firstweekday):
    if not MONDAY <= firstweekday <= SUNDAY:
        raise IllegalWeekdayError(firstweekday)
    c.firstweekday = firstweekday


def firstweekday():
    return c.firstweekday


def monthcalendar(year, month):
    return c.monthdayscalendar(year, month)


def prweek(theweek, width):
    c.prweek(theweek, width)


def week(theweek, width):
    return c.formatweek(theweek, width)


def weekheader(width):
    return c.formatweekheader(width)


def prmonth(theyear, themonth, w=0, l=0):
    c.prmonth(theyear, themonth, w, l)


def month(theyear, themonth, w=0, l=0):
    return c.formatmonth(theyear, themonth, w, l)


def calendar(theyear, w=2, l=1, c=6, m=3):
    return _tc.formatyear(theyear, w, l, c, m)


_tc = c


def prcal(theyear, w=0, l=0, c=6, m=3):
    _tc.pryear(theyear, w, l, c, m)


_EPOCH = 1970
_EPOCH_ORD = 719163


def timegm(tuple):
    year, month, day, hour, minute, second = tuple[:6]
    days = _ordinal(year, month, 1) - _EPOCH_ORD + day - 1
    hours = days * 24 + hour
    minutes = hours * 60 + minute
    seconds = minutes * 60 + second
    return seconds
