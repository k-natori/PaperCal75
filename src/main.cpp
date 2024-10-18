#include <Arduino.h>

#include <SPI.h>
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "SD_MMC.h"

#include <vector>
#include <map>
#include <time.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <Preferences.h>

#include <LovyanGFX.hpp>

#include "PCEvent.h"
#include "epd7in5b_V2.h"


#define SD_MMC_CMD 38
#define SD_MMC_CLK 39
#define SD_MMC_D0 40

#define LED_BUILTIN 2
#define PIN_BUTTON 4

#define VOLTAGE_TEST 8
#define VOLTAGE_READ 9
#define VOLTAGE_ADC_CHANNEL ADC_CHANNEL_8

#define screenWidth 800
#define screenHeight 480
#define HEADER_HEIGHT 0
#define FOOTER_HEIGHT 20
#define COLUMN_WIDTH 114
#define DAY_HEIGHT 34

#define WHITE 255
#define BLACK 0

#define LARGE_FONT FreeSansBold18pt7b
#define SMALL_FONT efontJA_12

#define uS_TO_S_FACTOR 1000000ULL

#define prefName "PaperCal"
#define holidayCacheKey "Holiday"
#define bootCountKey "Boot"

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

Preferences pref;
String holidayCacheString;
int bootCount;

LGFX_Sprite blackSprite;
LGFX_Sprite redSprite;

void showCalendar();
void loadICalendar(String urlString, boolean holiday);
uint32_t readVoltage();
void logLine(String line);
void shutdown(int wakeUpSeconds);

void setup()
{
  // put your setup code here, to run once:
  blackSprite.setColorDepth(1);
  blackSprite.createSprite(EPD_WIDTH, EPD_HEIGHT);
  blackSprite.setTextWrap(false);

  redSprite.setColorDepth(1);
  redSprite.createSprite(EPD_WIDTH, EPD_HEIGHT);
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
      PCEvent::setRootCA(pemFile.readString());
      pemFile.close();
      log_printf("pem file loaded:%s\n", pemFileName.c_str());
    }

        // Load Holidays cache for this month
    pref.begin(prefName, false);
    holidayCacheString = pref.getString(holidayCacheKey, "");
    bootCount = pref.getInt(bootCountKey, 0);
    pref.end();

    // Boot count
    esp_sleep_wakeup_cause_t wakeupCause = esp_sleep_get_wakeup_cause();
    switch (wakeupCause)
    {
    case ESP_SLEEP_WAKEUP_TIMER:
    {
      bootCount++;
      break;
    }
    default:
    {
      bootCount = 0;
    }
    }

    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(VOLTAGE_TEST, OUTPUT);
    pinMode(VOLTAGE_READ, ANALOG);
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
  PCEvent::setTimeInfo(timeinfo);
  PCEvent::setHolidayCacheString(holidayCacheString);

  int year = PCEvent::currentYear;
  int month = PCEvent::currentMonth;
  int day = PCEvent::currentDay;

  log_printf("%d/%d/%d\n", year, month, day);

  // Load iCalendar
  for (auto &urlString : iCalendarURLs)
  {
    PCEvent::loadICalendar(urlString, false);
  }
  // Load iCalendar for holidays
  if (!PCEvent::isCacheValid() && !iCalendarHolidayURL.isEmpty())
  {
    PCEvent::loadICalendar(iCalendarHolidayURL, true);
    pref.begin(prefName, false);
    pref.putString(holidayCacheKey, PCEvent::holidayCacheString());
    pref.end();
  }

  WiFi.disconnect(true);

  // Draw calendar
  int firstDayOfWeek = dayOfWeek(year, month, 1);
  int numberOfDays = numberOfDaysInMonth(year, month);
  int numberOfRows = (firstDayOfWeek + numberOfDays - 1) / 7 + 1;
  int rowHeight = (EPD_HEIGHT - FOOTER_HEIGHT) / numberOfRows;

  blackSprite.fillScreen(WHITE);
  redSprite.fillScreen(WHITE);

  // draw horizontal lines
  for (int i = 1; i <= numberOfRows; i++)
  {
    blackSprite.drawFastHLine(0, i * rowHeight, EPD_WIDTH, BLACK);
  }

  // draw vertical lines
  int lineHeight = numberOfRows * rowHeight;
  for (int i = 1; i < 7; i++)
  {
    blackSprite.drawFastVLine(i * COLUMN_WIDTH, 0, lineHeight, BLACK);
  }

  // draw days in month
  LGFX_Sprite *selectedSprite = &blackSprite;

  for (int i = 1; i <= numberOfDays; i++)
  {
    int row = (firstDayOfWeek + i - 1) / 7;
    int column = (6 + firstDayOfWeek + i) % 7;
    boolean holiday = (column == 0 || column == 6 || (PCEvent::numberOfHolidaysInDayOfThisMonth(i) > 0)) ? true : false;
    selectedSprite = holiday ? &redSprite : &blackSprite;

    // invert color if it is today
    uint16_t dayColor = BLACK;
    if (day == i)
    {
      selectedSprite->fillRect(column * COLUMN_WIDTH, row * rowHeight, COLUMN_WIDTH, DAY_HEIGHT, BLACK);
      dayColor = WHITE;
    }

    // draw day
    // widthOfString(String(i), true);
    selectedSprite->setFont(&fonts::LARGE_FONT);
    int dayWidth = selectedSprite->textWidth(String(i));
    selectedSprite->setTextColor(dayColor);
    selectedSprite->setCursor(column * COLUMN_WIDTH + (COLUMN_WIDTH - dayWidth) / 2, row * rowHeight + 4);
    selectedSprite->printf("%d", i);

    // draw events
    std::vector<PCEvent> eventsInToday;
    auto holidaysInDay = PCEvent::holidaysInDayOfThisMonth(i);
    eventsInToday.insert(eventsInToday.end(), holidaysInDay.begin(), holidaysInDay.end());
    auto eventsInDay = PCEvent::eventsInDayOfThisMonth(i);
    eventsInToday.insert(eventsInToday.end(), eventsInDay.begin(), eventsInDay.end());
    
    blackSprite.setFont(&fonts::SMALL_FONT);
    redSprite.setFont(&fonts::SMALL_FONT);
    blackSprite.setTextColor(BLACK);
    redSprite.setTextColor(BLACK);
    blackSprite.setClipRect(column * COLUMN_WIDTH, row * rowHeight + DAY_HEIGHT, COLUMN_WIDTH, rowHeight - DAY_HEIGHT);
    redSprite.setClipRect(column * COLUMN_WIDTH, row * rowHeight + DAY_HEIGHT, COLUMN_WIDTH, rowHeight - DAY_HEIGHT);
    if (eventsInToday.size() > 0)
    {
      int i = 0;
      for (auto &event : eventsInToday)
      {
        selectedSprite = event.isHolidayEvent ? &redSprite : &blackSprite;
        selectedSprite->setCursor(column * COLUMN_WIDTH + 2, row * rowHeight + DAY_HEIGHT + 13 * i);
        selectedSprite->print("・" + event.getTitle());
        i++;
        if (i >= 3)
          break;
      }
    }
    blackSprite.clearClipRect();
    redSprite.clearClipRect();

  }


  // Log date
  char logBuffer[32];
  sprintf(logBuffer, "%d/%d/%d %02d:%02d:%02d", year, month, day, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  String logString = String(logBuffer);

  // Log events
  logString += ", Events:";
  logString += String(PCEvent::numberOfEventsInThisMonth());

  // Log boot count
  logString += ", Boot:";
  logString += String(bootCount);

  // Save boot count
  pref.begin(prefName, false);
  pref.putInt(bootCountKey, bootCount);
  pref.end();

  // Footer
  // uint32_t voltage = readVoltage();
  blackSprite.setFont(&fonts::SMALL_FONT);
  blackSprite.setCursor(8, EPD_HEIGHT - FOOTER_HEIGHT);
  blackSprite.print(logString);

  Epd epd;
  int initResult = epd.Init();
  if (initResult != 0)
  {
    log_printf("e-Paper init failed: %d", initResult);
    Serial.print("e-Paper init failed");
    return;
  }
  epd.Displaypart((unsigned char *)(blackSprite.getBuffer()), 0, 0, EPD_WIDTH, EPD_HEIGHT, 0);
  epd.Displaypart((unsigned char *)(redSprite.getBuffer()), 0, 0, EPD_WIDTH, EPD_HEIGHT, 1);
  epd.Sleep();
  

  // Deep sleep
  loaded = true;
  digitalWrite(LED_BUILTIN, LOW);
  delay(1000);
  shutdown(24 * 3600 - (timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60 + timeinfo.tm_sec) + 300);
}


uint32_t readVoltage()
{
  uint32_t voltage = 0;
  esp_adc_cal_characteristics_t characteristics;
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_12Bit, 1100, &characteristics);
  digitalWrite(VOLTAGE_TEST, HIGH);
  if (esp_adc_cal_get_voltage(ADC_CHANNEL_8, &characteristics, &voltage) == ESP_OK)
  {

    digitalWrite(VOLTAGE_TEST, LOW);
    return voltage;
  }
  digitalWrite(VOLTAGE_TEST, LOW);
  return 0;
}

void logLine(String line) {
  File logFile = SD_MMC.open("/log.txt", FILE_APPEND, true);
  if (logFile)
  {
    logFile.println(line);
  }
  logFile.close();
}

void shutdown(int wakeUpSeconds)
{
  esp_sleep_enable_timer_wakeup(wakeUpSeconds * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}