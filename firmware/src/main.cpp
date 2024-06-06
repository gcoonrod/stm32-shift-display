#include <Arduino.h>
#include <ShiftDisplay.h>
#include <STM32RTC.h>
#include <AceButton.h>
#include "header.h"

using namespace ace_button;

#define SER PB3
#define SRCLK PB4
#define SRCLRB PB5
#define RCLK PB6
#define OEB PB7

#define LED3 PA0
#define LED2 PA1
#define LED1 PA2
//#define LED1 LED_BUILTIN
#define BTN1 PA3
#define BTN2 PA4
#define BTN3 PA5

/**
 * Bit Position:  7   6   5   5   3   2   1   0
 * 595 Outputs:   QA  QB  QC  QD  QE  QF  QG  QH
 * 7 Segment LED: D   E   G   F   A   B   DP  C
 */

ShiftDisplay display(SER, SRCLK, SRCLRB, RCLK, OEB);
STM32RTC &rtc = STM32RTC::getInstance();
DateTimeBuffer_t date_time_buf = {0, 1, RTC_MONTH_JANUARY, 1, 0, 0, 0};
bool time_dirty = true;

AceButton btn1(BTN1);
AceButton btn2(BTN2);
AceButton btn3(BTN3);

uint8_t weekDay;
uint8_t day;
uint8_t month;
uint8_t year;

uint16_t counter = 0;

bool toggling = false;

/**
 * TODOs:
 * 1. Store display state in a volatile buffer. Update display from buffer periodically.
 * 2. (DONE) Setup RTC interrupt and update date/time variables from there.
 * 3. Begin work on an FSM to manage the "menu".
 * 4. ...
*/

// Function definitions
void setup_user_leds();
void setup_user_btns();
void setup_rtc();
void setup_usb();
void printClock();

// IRQ Handlers and Event Callbacks
void handleEvent(AceButton *, uint8_t, uint8_t);
void irq_rtc_seconds(void *data);

void setup()
{
  setup_rtc();
  setup_user_btns();
  setup_user_leds();

  display.begin();
  display.enable();
}

void loop()
{
  if (time_dirty)
  {
    display.clear();
    printClock();
    display.latch();
    time_dirty = false;
  }

  btn1.check();
  btn2.check();
  btn3.check();

  // Update LEDs
  digitalWrite(LED3, toggling);
}

void setup_user_leds()
{
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);

  digitalWrite(LED1, LOW);
  digitalWrite(LED2, LOW);
  digitalWrite(LED3, LOW);
}

void setup_user_btns()
{
  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BTN3, INPUT_PULLUP);

  ButtonConfig *buttonConfig = ButtonConfig::getSystemButtonConfig();
  buttonConfig->setEventHandler(handleEvent);
  buttonConfig->setFeature(ButtonConfig::kFeatureClick);
  buttonConfig->setFeature(ButtonConfig::kFeatureDoubleClick);
  buttonConfig->setFeature(ButtonConfig::kFeatureLongPress);
  buttonConfig->setFeature(ButtonConfig::kFeatureRepeatPress);
}

void setup_rtc()
{
  rtc.setClockSource(STM32RTC::LSE_CLOCK);
  delay(500);
  rtc.begin(false, STM32RTC::HOUR_24);

  // get date from rtc backup
  rtc.getDate(&date_time_buf.week_day, &date_time_buf.day, &date_time_buf.month, &date_time_buf.year);

  // if it is the epoch we need to reset, otherwise carry on.
  if ((day == 1) && (month == RTC_MONTH_JANUARY) && (year == 1))
  {
    // Unix Epoch
    rtc.setHours(date_time_buf.hours);
    rtc.setMinutes(date_time_buf.minutes);
    rtc.setSeconds(date_time_buf.seconds);
    rtc.setSubSeconds(0);

    rtc.setWeekDay(date_time_buf.week_day);
    rtc.setDay(date_time_buf.day);
    rtc.setMonth(date_time_buf.month);
    rtc.setYear(date_time_buf.year);
  }

  rtc.attachSecondsInterrupt(irq_rtc_seconds);
}

void setup_usb()
{
  // TODO: Configure USB as virtual com, HID, or DFU depending on boot mode
}

void printClock()
{
  uint8_t seconds = date_time_buf.seconds;
  uint8_t minutes = date_time_buf.minutes;
  uint8_t hours = date_time_buf.hours;

  display.shiftOutAscii((seconds % 10) + '0');
  display.shiftOutAscii((seconds / 10) + '0');

  display.shiftOutAscii((minutes % 10) + '0', true);
  display.shiftOutAscii((minutes / 10) + '0');

  display.shiftOutAscii((hours % 10) + '0');
  display.shiftOutAscii((hours / 10) + '0');
}

void handleEvent(AceButton *button, uint8_t eventType, uint8_t buttonState)
{
  switch (eventType)
  {
  case AceButton::kEventPressed:
    if (button->getPin() == BTN1)
    {
      digitalWrite(LED1, HIGH);
    }
    break;

  case AceButton::kEventReleased:
    if (button->getPin() == BTN1)
    {
      digitalWrite(LED1, LOW);
    }
    break;

  default:
    break;
  }
}

void irq_rtc_seconds(void *data)
{
  UNUSED(data);
  
  // Update the clock buffer
  // TODO: How long does this take? Should perhaps just update the time and check
  // if the date needs updating occasionally in the main loop.
  date_time_buf.year = rtc.getYear();
  date_time_buf.month = rtc.getMonth();
  date_time_buf.day = rtc.getDay();
  date_time_buf.week_day = rtc.getWeekDay();
  date_time_buf.hours = rtc.getHours();
  date_time_buf.minutes = rtc.getMinutes();
  date_time_buf.seconds = rtc.getSeconds();

  time_dirty = true;
}
