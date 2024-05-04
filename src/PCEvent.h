#ifndef PCEVENT_H_INCLUDE
#define PCEVENT_H_INCLUDE

#include <Arduino.h>
#include <map>
#include <time.h>

int dayOfWeek(int year, int month, int day);
int numberOfDaysInMonth(int year, int month);
tm tmFromICalDateString(String iCalDateString, float toTimezone);


class PCEvent
{
public:
    PCEvent(String sourceString, float toTimezone);
    time_t getTimeT() const;
    int getYear();
    int getMonth();
    int getDay();
    int getDayOfWeek();
    int getHour();
    int getMinute();
    int getSecond();
    String descriptionForDay(boolean isToday);
    double duration();
    String getTitle();
    boolean isHolidayEvent;

private:
    tm startTM;
    tm endTM;
    boolean isDayEvent;
    float timezone;
    String title;
};


bool operator<(const PCEvent&left, const PCEvent&right) ;
bool operator>(const PCEvent&left, const PCEvent&right) ;

#endif