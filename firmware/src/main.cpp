#include <Arduino.h>
#include <ShiftDisplay.h>
#include <ShiftClock.h>

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

const char message[] = {' ', 'D', 'E', 'A', 'D', ' ', 'B', 'E', 'E', 'F'};
int idx = 0;
const size_t message_len = sizeof(message) / sizeof(message[0]);

ShiftDisplay display(SER, SRCLK, SRCLRB, RCLK, OEB);
ShiftClock shiftClock(0, 0, 0);

// Function definitions
void setup_user_leds();
void setup_user_btns();
void setup_rtc();
void setup_usb();

void printClock();



void setup()
{
  Serial.begin(115200);

  display.begin();
  display.enable();

  shiftClock.setHours(23);
  shiftClock.setMinutes(59);
  shiftClock.setSeconds(55);
}

void loop()
{
  printClock();
  delay(950);
  shiftClock.tick();
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
  display.shiftOutAscii((shiftClock.getSeconds() % 10) + '0');
  display.shiftOutAscii((shiftClock.getSeconds() / 10) + '0');

  display.shiftOutAscii((shiftClock.getMinutes() % 10) + '0', true);
  display.shiftOutAscii((shiftClock.getMinutes() / 10) + '0');

  display.shiftOutAscii((shiftClock.getHours() % 10) + '0');
  display.shiftOutAscii((shiftClock.getHours() / 10) + '0');
}