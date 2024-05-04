#include "PCEvent.h"
#include "NJScanner.h"

PCEvent::PCEvent(String sourceString, float toTimezone)
{
    NJScanner scanner = NJScanner(sourceString);
    do
    {
        String key = scanner.scanUpToString(":", true);
        key.trim();
        String content = scanner.scanUpToString("\r\n", true);
        content.trim();

        if (key == "DTSTART;VALUE=DATE")
        {
            startTM = tmFromICalDateString(content, 0.0f);
            isDayEvent = true;
        }
        else if (key == "DTSTART")
        {
            startTM = tmFromICalDateString(content, toTimezone);
            isDayEvent = false;
        }
        else if (key.startsWith("DTEND"))
        {
            endTM = tmFromICalDateString(content, toTimezone);
        }
        else if (key == "SUMMARY")
        {
            title = content;
        }
    } while (!scanner.isAtEnd());
}

time_t PCEvent::getTimeT() const
{
    tm temp = startTM;
    return mktime(&temp);
}
int PCEvent::getYear()
{
    return startTM.tm_year + 1900;
}
int PCEvent::getMonth()
{
    return startTM.tm_mon + 1;
}
int PCEvent::getDay()
{
    return startTM.tm_mday;
}
int PCEvent::getDayOfWeek()
{
    return startTM.tm_wday;
}
int PCEvent::getHour()
{
    return startTM.tm_hour;
}
int PCEvent::getMinute()
{
    return startTM.tm_min;
}
int PCEvent::getSecond()
{
    return startTM.tm_sec;
}

String PCEvent::descriptionForDay(boolean isToday)
{
    if (isToday)
    {
        if (startTM.tm_hour > 0)
        {
            char buf[6];
            sprintf(buf, "%02d:%02d", startTM.tm_hour, startTM.tm_min);
            return String(buf);
        }
        else
        {
            return "Today:";
        }
    }
    char buf[6];
    sprintf(buf, "%d/%d", startTM.tm_mon + 1, startTM.tm_mday);
    return String(buf);
}

double PCEvent::duration()
{
    return difftime(mktime(&endTM), mktime(&startTM));
}
String PCEvent::getTitle()
{
    return title;
}

bool operator<(const PCEvent &left, const PCEvent &right)
{
    return (left.getTimeT() < right.getTimeT());
}
bool operator>(const PCEvent &left, const PCEvent &right)
{
    return (left.getTimeT() > right.getTimeT());
}

int dayOfWeek(int year, int month, int day)
{
    if (month < 3)
    {
        year--;
        month += 12;
    }
    return (year + year / 4 - year / 100 + year / 400 + (13 * month + 8) / 5 + day) % 7;
}

int numberOfDaysInMonth(int year, int month)
{
    int numberOfDaysInMonthArray[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (year % 4 == 0 && year % 100 != 0 || year % 400 == 0)
    {
        numberOfDaysInMonthArray[1] = 29;
    }
    return numberOfDaysInMonthArray[month - 1];
}

tm tmFromICalDateString(String iCalDateString, float toTimezone)
{
    // 20240122T051119Z
    // YYYYMMDD T hhmmss Z
    tm timeInfo = {.tm_sec = 0, .tm_min = 0, .tm_hour = 0, .tm_mday = 0, .tm_mon = 0, .tm_year = 0};
    if (iCalDateString.length() < 8)
        return timeInfo;
    timeInfo.tm_year = (iCalDateString.substring(0, 4)).toInt() - 1900;
    timeInfo.tm_mon = (iCalDateString.substring(4, 6)).toInt() - 1;
    timeInfo.tm_mday = (iCalDateString.substring(6, 8)).toInt();
    timeInfo.tm_wday = dayOfWeek(timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday);

    if (iCalDateString.length() < 15)
        return timeInfo;
    if (iCalDateString.charAt(8) != 'T')
        return timeInfo;

    timeInfo.tm_hour = (iCalDateString.substring(9, 11)).toInt();
    timeInfo.tm_min = (iCalDateString.substring(11, 13)).toInt();
    timeInfo.tm_sec = (iCalDateString.substring(13, 15)).toInt();

    // Convert timezone
    if (toTimezone != 0.0f)
    {
        int numberOfDaysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if ((timeInfo.tm_year + 1900) % 4 == 0 && (timeInfo.tm_year + 1900) % 100 != 0 || (timeInfo.tm_year + 1900) % 400 == 0)
        {
            numberOfDaysInMonth[1] = 29;
        }

        int secondsInDay = timeInfo.tm_hour * 3600 + timeInfo.tm_min * 60 + timeInfo.tm_sec;
        int convertedSecondsInDay = secondsInDay + toTimezone * 3600;

        if (convertedSecondsInDay < 0)
        { // previous day
            if (timeInfo.tm_mday == 1)
            {
                if (timeInfo.tm_mon == 0)
                { // January
                    if (timeInfo.tm_year > 1)
                    {
                        timeInfo.tm_year--;
                    }
                    timeInfo.tm_mon = 11;
                }
                else
                {
                    timeInfo.tm_mon--;
                }
                timeInfo.tm_mday = numberOfDaysInMonth[timeInfo.tm_mon];
            }
            else
            {
                timeInfo.tm_mday--;
            }
            convertedSecondsInDay += 24 * 3600;
        }
        else if (convertedSecondsInDay >= 24 * 3600)
        { // next day
            if (timeInfo.tm_mday == numberOfDaysInMonth[timeInfo.tm_mon])
            {
                if (timeInfo.tm_mon == 11)
                {
                    timeInfo.tm_year++;
                    timeInfo.tm_mon = 0;
                }
                else
                {
                    timeInfo.tm_mon++;
                }
                timeInfo.tm_mday = 1;
            }
            else
            {
                timeInfo.tm_mday++;
            }
            convertedSecondsInDay -= 24 * 3600;
        }
        timeInfo.tm_hour = convertedSecondsInDay / 3600;
        timeInfo.tm_min = (convertedSecondsInDay % 3600) / 60;
        timeInfo.tm_sec = (convertedSecondsInDay % 3600) % 60;
        timeInfo.tm_wday = dayOfWeek(timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday);
    }

    return timeInfo;
}