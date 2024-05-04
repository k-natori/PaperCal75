#include <Arduino.h>

#include <SPI.h>
#include "epd7in5b_V2.h"

#include <vector>
#include <map>
#include <time.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include "SD_MMC.h"

#include <LovyanGFX.hpp>

#include "PCEvent.h"


#define SD_MMC_CMD 38
#define SD_MMC_CLK 39
#define SD_MMC_D0 40

#define LED_BUILTIN 2
#define PIN_BUTTON 4

#define screenWidth 800
#define screenHeight 480
#define columnWidth 114
#define dayHeight 34
#define maxNumberOfRows 10

#define WHITE 255
#define BLACK 0

#define LARGE_FONT FreeSansBold18pt7b
#define SMALL_FONT efontJA_12

String pemFileName = "/root_ca.pem";
std::vector<String> iCalendarURLs;
String iCalendarHolidayURL;
String rootCA = "";
boolean loaded = false;
boolean loginScreen = false;
float timezone = 0;

int currentYear = 0;
int currentMonth = 0;
int nextMonthYear = 0;
int nextMonth = 0;
String dateString = "";

std::multimap<int, PCEvent> eventsInThisMonth;
std::multimap<int, PCEvent> holidaysInThisMonth;
std::vector<PCEvent> eventsInNextMonth;
std::vector<PCEvent> eventsToDisplay;

LGFX_Sprite blackSprite;
LGFX_Sprite redSprite;

void showCalendar();
void loadICalendar(String urlString, boolean holiday);

void setup()
{
  // put your setup code here, to run once:
  blackSprite.setColorDepth(1);
  blackSprite.createSprite(screenWidth, screenHeight);
  blackSprite.setTextWrap(false);

  redSprite.setColorDepth(1);
  redSprite.createSprite(screenWidth, screenHeight);
  redSprite.setTextWrap(false);

  // SD Card
  SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
  if (!SD_MMC.begin("/sdcard", true, true, SDMMC_FREQ_DEFAULT, 5))
  {
    log_printf("Card Mount Failed\n");
    return;
  }
  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE)
  {
    log_printf("No SD_MMC card attached\n");
    return;
  }

  // Load settings from "settings.txt" in SD card
  String wifiIDString = "wifiID";
  String wifiPWString = "wifiPW";

  File settingFile = SD_MMC.open("/settings.txt");
  if (settingFile)
  {
    while (settingFile.available() > 0)
    {
      String line = settingFile.readStringUntil('\n');
      if (line.startsWith("//"))
        continue;
      int separatorLocation = line.indexOf(":");
      if (separatorLocation > -1)
      {
        String key = line.substring(0, separatorLocation);
        String content = line.substring(separatorLocation + 1);

        // WiFi SSID and paassword
        if (key == "SSID")
          wifiIDString = content;

        else if (key == "PASS")
          wifiPWString = content;

        // HTTPS access
        else if (key == "pemFileName")
          pemFileName = content;

        else if (key == "iCalendarURL")
          iCalendarURLs.push_back(content);

        else if (key == "holidayURL")
          iCalendarHolidayURL = content;

        else if (key == "timezone")
          timezone = content.toFloat();
      }
    }
    settingFile.close();

    // Start Wifi connection
    WiFi.begin(wifiIDString.c_str(), wifiPWString.c_str());
    // Wait until wifi connected
    int i = 0;
    while (WiFi.status() != WL_CONNECTED)
    {
      delay(500);
      log_printf(".");
      i++;
      if (i > 120)
        break;
    }
    log_printf("\n");

    // Setup NTP
    configTime(60 * 60 * timezone, 0, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");

    // Load PEM file in SD card
    File pemFile = SD_MMC.open(pemFileName.c_str());
    if (pemFile)
    {
      rootCA = pemFile.readString();
      pemFile.close();
      log_printf("pem file loaded:%s\n", pemFileName.c_str());
    }

    pinMode(LED_BUILTIN, OUTPUT);
  }
}

void loop()
{
  // put your main code here, to run repeatedly:
  if (!loaded)
  {
    digitalWrite(LED_BUILTIN, HIGH);
    showCalendar();
    log_printf("Free heap size: %d bytes\n", esp_get_free_heap_size());
    loaded = true;
    delay(1000);
    digitalWrite(LED_BUILTIN, LOW);
  }
}

void showCalendar()
{
  // Get local time
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    log_printf("Waiting getLocalTime\n");
    delay(500);
    return;
  }

  int year = timeinfo.tm_year + 1900;
  int month = timeinfo.tm_mon + 1;
  int day = timeinfo.tm_mday;
  currentYear = year;
  currentMonth = month;
  if (currentMonth == 12)
  {
    nextMonthYear = currentYear + 1;
    nextMonth = 1;
  }
  else
  {
    nextMonthYear = currentYear;
    nextMonth = currentMonth + 1;
  }

  log_printf("%d/%d/%d\n", year, month, day);

  // Load iCalendar
  for (auto &urlString : iCalendarURLs)
  {
    loadICalendar(urlString, false);
  }
  // loadICalendar(iCalendarURL);
  if (!iCalendarHolidayURL.isEmpty())
  {
    loadICalendar(iCalendarHolidayURL, true);
  }

  // Draw calendar
  int firstDayOfWeek = dayOfWeek(year, month, 1);
  int numberOfDays = numberOfDaysInMonth(year, month);
  int numberOfRows = (firstDayOfWeek + numberOfDays - 1) / 7 + 1;
  int rowHeight = (screenHeight - 30) / numberOfRows;

  blackSprite.fillScreen(WHITE);
  redSprite.fillScreen(WHITE);

  // draw horizontal lines
  for (int i = 1; i <= numberOfRows; i++)
  {
    blackSprite.drawFastHLine(0, i * rowHeight, screenWidth, BLACK);
  }

  // draw vertical lines
  int lineHeight = numberOfRows * rowHeight;
  for (int i = 1; i < 7; i++)
  {
    blackSprite.drawFastVLine(i * columnWidth, 0, lineHeight, BLACK);
  }

  // draw days in month
  LGFX_Sprite *selectedSprite = &blackSprite;

  for (int i = 1; i <= numberOfDays; i++)
  {
    int row = (firstDayOfWeek + i - 1) / 7;
    int column = (6 + firstDayOfWeek + i) % 7;
    boolean holiday = (column == 0 || column == 6 || (holidaysInThisMonth.count(i) > 0)) ? true : false;
    selectedSprite = holiday ? &redSprite : &blackSprite;

    // invert color if it is today
    uint16_t dayColor = BLACK;
    if (day == i)
    {
      selectedSprite->fillRect(column * columnWidth, row * rowHeight, columnWidth, dayHeight, BLACK);
      dayColor = WHITE;
    }

    // draw day
    // widthOfString(String(i), true);
    selectedSprite->setFont(&fonts::LARGE_FONT);
    int dayWidth = selectedSprite->textWidth(String(i));
    selectedSprite->setTextColor(dayColor);
    selectedSprite->setCursor(column * columnWidth + (columnWidth - dayWidth) / 2, row * rowHeight + 4);
    selectedSprite->printf("%d", i);

    // draw events
    std::vector<PCEvent> eventsInToday;
    if (holidaysInThisMonth.count(i) > 0)
    {
      auto itr = holidaysInThisMonth.lower_bound(i);
      auto last = holidaysInThisMonth.upper_bound(i);
      while (itr != last)
      {
        eventsInToday.push_back(itr->second);
        ++itr;
      }
    }
    if (eventsInThisMonth.count(i) > 0)
    {
      auto itr = eventsInThisMonth.lower_bound(i);
      auto last = eventsInThisMonth.upper_bound(i);
      while (itr != last)
      {
        eventsInToday.push_back(itr->second);
        ++itr;
      }
    }
    blackSprite.setFont(&fonts::SMALL_FONT);
    redSprite.setFont(&fonts::SMALL_FONT);
    blackSprite.setTextColor(BLACK);
    redSprite.setTextColor(BLACK);
    blackSprite.setClipRect(column * columnWidth, row * rowHeight + dayHeight, columnWidth, rowHeight - dayHeight);
    redSprite.setClipRect(column * columnWidth, row * rowHeight + dayHeight, columnWidth, rowHeight - dayHeight);
    if (eventsInToday.size() > 0)
    {
      int i = 0;
      for (auto &event : eventsInToday)
      {
        selectedSprite = event.isHolidayEvent ? &redSprite : &blackSprite;
        selectedSprite->setCursor(column * columnWidth + 2, row * rowHeight + dayHeight + 13 * i);
        selectedSprite->print("・" + event.getTitle());
        i++;
        if (i >= 3)
          break;
      }
    }
    blackSprite.clearClipRect();
    redSprite.clearClipRect();

  }

  // Footer

  blackSprite.setFont(&fonts::SMALL_FONT);
  blackSprite.setCursor(8, screenHeight - 20);
  blackSprite.printf("%d/%d/%d %02d:%02d:%02d", year, month, day, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

  Epd epd;
  int initResult = epd.Init();
  if (initResult != 0)
  {
    log_printf("e-Paper init failed: %d", initResult);
    Serial.print("e-Paper init failed");
    return;
  }
  epd.Displaypart((unsigned char *)(blackSprite.getBuffer()), 0, 0, screenWidth, screenHeight, 0);
  epd.Displaypart((unsigned char *)(redSprite.getBuffer()), 0, 0, screenWidth, screenHeight, 1);
  epd.Sleep();
}

void loadICalendar(String urlString, boolean holiday)
{
  HTTPClient httpClient;
  httpClient.begin(urlString, rootCA.c_str());
  dateString = "";
  String lastModified = "";

  const char *headerKeys[] = {"Date", "Last-Modified"};
  httpClient.collectHeaders(headerKeys, 1);

  int result = httpClient.GET();
  if (result == HTTP_CODE_OK)
  {
    dateString = httpClient.header("Date");
    lastModified = httpClient.header("Last-Modified");
    Serial.println("Last-Modified:" + lastModified);
    WiFiClient *stream = httpClient.getStreamPtr();
    if (httpClient.connected())
    {
      String previousLine = "";
      String eventBlock = "";
      boolean loadingEvent = false;
      while (stream->available())
      {
        String line = stream->readStringUntil('\n');
        if (line.indexOf(":") < 0) {
          line = stream->readStringUntil('\n');
          line = previousLine + line;
          previousLine = "";
        } else {
          previousLine = line;
        }

        if (line.startsWith("BEGIN:VEVENT"))
        { // begin VEVENT block
          loadingEvent = true;
        }
        else if (line.startsWith("DTSTART"))
        { // read start date
          int position = line.indexOf(":");
          String timeString = line.substring(position + 1);
          timeString.trim();
          tm timeInfo = tmFromICalDateString(timeString, timezone);
          if (!(timeInfo.tm_year + 1900 == currentYear && timeInfo.tm_mon + 1 == currentMonth) && !(timeInfo.tm_year + 1900 == nextMonthYear && timeInfo.tm_mon + 1 == nextMonth))
          {
            // discard event if not scheduled in this month to next month
            loadingEvent = false;
            eventBlock = "";
          }
        }

        if (loadingEvent)
        {
          eventBlock += line + "\n";
        }
        if (loadingEvent && line.startsWith("END:VEVENT"))
        {
          loadingEvent = false;
          PCEvent event = PCEvent(eventBlock, timezone);
          event.isHolidayEvent = holiday;
          if (event.getMonth() == currentMonth)
          {
            // Will be displayed as this month
            if (holiday)
            {
              holidaysInThisMonth.insert(std::make_pair(event.getDay(), event));
            }
            else
            {
              eventsInThisMonth.insert(std::make_pair(event.getDay(), event));
            }
          }
          else
          {
            // Next month
            eventsInNextMonth.push_back(event);
          }
          eventBlock = "";
        }
      }
    }
    httpClient.end();
  }
  else
  {
    // HTTP Error
    httpClient.end();
    // shutdownWithMessage("HTTP Error Code:" + result, 60*60*24);
  }
}
