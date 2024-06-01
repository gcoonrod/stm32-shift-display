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

void shiftOutByte(uint8_t ulVal);
uint8_t ascii_to_seven_segment(char hex_char);

void setup()
{
  display.begin();
  display.enable();

  display.print(F("12"));
  delay(1000);
  display.print(F("23  "));
  delay(1000);
  display.println(F("DEAD"));
}

void loop()
{
  // for (int i = message_len - 1; i >= 0; i--)
  // {
  //   display.shiftOutAscii(message[i]);
  //   delay(500);
  // }
}

/**
 * Initialize the GPIOs that control the 595 chain. Leave output disabled (OEB high).
*/
void setup_shift_reg_ctrl()
{
  pinMode(SER, OUTPUT);
  pinMode(SRCLK, OUTPUT);
  pinMode(SRCLRB, OUTPUT);
  pinMode(RCLK, OUTPUT);
  pinMode(OEB, OUTPUT);

  digitalWrite(SER, LOW);
  digitalWrite(SRCLK, LOW);
  digitalWrite(SRCLRB, HIGH);
  digitalWrite(RCLK, LOW);
  digitalWrite(OEB, HIGH);
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

void shiftOutByte(uint8_t ulVal)
{
  uint8_t i;

  for (i = 0; i < 8; i++)
  {
    digitalWrite(SER, !!(ulVal & (1 << i)));
    delayMicroseconds(1);

    digitalWrite(SRCLK, HIGH);
    digitalWrite(RCLK, HIGH);
    delayMicroseconds(1);
    digitalWrite(SRCLK, LOW);
    digitalWrite(RCLK, LOW);
    delayMicroseconds(1);
  }
}

uint8_t ascii_to_seven_segment(char hex_char)
{
  static const uint8_t segment_data[] = {
      0b11011101, // '0'
      0b00000101, // '1'
      0b11101100, // '2'
      0b00101101, // '3'
      0b00110101, // '4'
      0b10111001, // '5'
      0b11111001, // '6'
      0b00001101, // '7'
      0b11111101, // '8'
      0b10111101, // '9'
      0b01111101, // 'A'
      0b11110001, // 'b'
      0b11011000, // 'C'
      0b11100101, // 'd'
      0b11111000, // 'E'
      0b01111000  // 'F'
  };

  // Error checking: ensure input is a valid hexadecimal character
  if (!((hex_char >= '0' && hex_char <= '9') || (hex_char >= 'A' && hex_char <= 'F')))
  {
    if (hex_char == ' ')
    {
      return 0;
    }
    return 0b10101000;
  }

  // Convert character to index (subtract ASCII offset)
  uint8_t index = (hex_char <= '9') ? (hex_char - '0') : (hex_char - 'A' + 10);

  // Return the corresponding seven-segment pattern
  return segment_data[index];
}
