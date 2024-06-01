#include <Arduino.h>
#include <ShiftDisplay.h>

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

// Function definitions
void setup_user_leds();
void setup_user_btns();
void setup_rtc();
void setup_usb();

void setup()
{
  Serial.begin(115200);

  display.begin();
  display.enable();

  // Print fake time 1635.24
  display.shiftOutAscii('4');
  display.shiftOutAscii('2');
  display.shiftOutAscii('5', true);
  display.shiftOutAscii('3');
  display.shiftOutAscii('6');
  display.shiftOutAscii('1');
}

void loop()
{
  // for (int i = message_len - 1; i >= 0; i--)
  // {
  //   display.shiftOutAscii(message[i]);
  //   delay(500);
  // }
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
