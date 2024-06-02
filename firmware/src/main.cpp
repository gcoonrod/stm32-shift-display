#include <Arduino.h>
#include <ShiftDisplay.h>
#include <STM32RTC.h>

#define SER PB3
#define SRCLK PB4
#define SRCLRB PB5
#define RCLK PB6
#define OEB PB7

#define LED3 PA0
#define LED2 PA1
#define LED1 PA2
#define BTN1 PA3
#define BTN2 PA4
#define BTN3 PA5

/**
 * Bit Position:  7   6   5   5   3   2   1   0
 * 595 Outputs:   QA  QB  QC  QD  QE  QF  QG  QH
 * 7 Segment LED: D   E   G   F   A   B   DP  C
 */

ShiftDisplay display(SER, SRCLK, SRCLRB, RCLK, OEB);
STM32RTC& rtc = STM32RTC::getInstance();

uint8_t weekDay;
uint8_t day;
uint8_t month;
uint8_t year;

// Function definitions
void setup_user_leds();
void setup_user_btns();
void setup_rtc();
void setup_usb();
void printClock();

void setup()
{

  rtc.setClockSource(STM32RTC::LSE_CLOCK);
  delay(500);
  rtc.begin(false, STM32RTC::HOUR_24);

  // get date from rtc backup
  rtc.getDate(&weekDay, &day, &month, &year);

  // if it is the epoch we need to reset, otherwise carry on.
  if ((day == 1) && (month == RTC_MONTH_JANUARY) && (year == 1)) {
    // Unix Epoch
    rtc.setHours(0);
    rtc.setMinutes(0);
    rtc.setSeconds(0);
    rtc.setSubSeconds(0);

    rtc.setWeekDay(RTC_WEEKDAY_SATURDAY);
    rtc.setDay(1);
    rtc.setMonth(6);
    rtc.setYear(24);
  } 

  display.begin();
  display.enable();
}

void loop()
{
  display.clear();
  printClock();
  display.latch();
  delay(1000);
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
}

void setup_rtc()
{
  // TODO: Configure the RTC clock and calendar. Recover from powerloss.
}

void setup_usb()
{
  // TODO: Configure USB as virtual com, HID, or DFU depending on boot mode
}

void printClock()
{
  uint8_t seconds = rtc.getSeconds();
  uint8_t minutes = rtc.getMinutes();
  uint8_t hours = rtc.getHours();

  display.shiftOutAscii((seconds % 10) + '0');
  display.shiftOutAscii((seconds / 10) + '0');

  display.shiftOutAscii((minutes % 10) + '0', true);
  display.shiftOutAscii((minutes / 10) + '0');

  display.shiftOutAscii((hours % 10) + '0');
  display.shiftOutAscii((hours / 10) + '0');
}