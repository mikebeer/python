import calendar
print(calendar.isleap(2024), calendar.isleap(1900), calendar.isleap(2000), calendar.leapdays(2000, 2025), calendar.weekday(2024, 2, 29), calendar.weekday(1970, 1, 1), calendar.weekday(2000, 12, 31))
print(calendar.monthrange(2024, 2), calendar.monthrange(2023, 2), calendar.monthrange(2025, 12))
print(calendar.month(2024, 2))
print(calendar.month(2025, 1, w=3, l=2))
print(calendar.monthcalendar(2024, 3))
print(calendar.calendar(2024))
print(calendar.month_name[3], calendar.month_abbr[12], calendar.day_name[0], calendar.day_abbr[6], list(calendar.day_abbr), len(calendar.month_name))
calendar.setfirstweekday(calendar.SUNDAY)
print(calendar.month(2024, 9), calendar.firstweekday())
calendar.setfirstweekday(0)
print(calendar.timegm((2024, 1, 1, 0, 0, 0)), calendar.timegm((1970, 1, 1, 0, 0, 1)), calendar.timegm((2000, 2, 29, 12, 30, 15, 0, 0, 0)))
cal = calendar.Calendar(6)
print(list(cal.itermonthdays(2024, 2)), list(cal.iterweekdays()), cal.monthdays2calendar(2024, 2)[0])
print(list(cal.itermonthdates(2024, 2))[:3], len(cal.yeardayscalendar(2024)), cal.monthdatescalendar(2024, 1)[0][0])
tc = calendar.TextCalendar(calendar.MONDAY)
print(tc.formatmonth(2023, 10), tc.formatweekheader(3), tc.formatmonthname(2020, 5, 20))
tc.prmonth(2024, 7)
print(calendar.HTMLCalendar().formatmonth(2024, 2))
try:
    calendar.monthrange(2024, 13)
except calendar.IllegalMonthError as e:
    print(e)
try:
    calendar.setfirstweekday(9)
except calendar.IllegalWeekdayError as e:
    print(e)
