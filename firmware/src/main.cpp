#include <Arduino.h>
#include <ShiftDisplay.h>
#include <ShiftDisplayFSM.h>
#include <STM32RTC.h>
#include <AceButton.h>
#include <SerialCommands.h>
#include "header.h"

#define SERIAL_COMMANDS_DEBUG

using namespace ace_button;

#define SER PA3
#define SRCLK PA4
#define SRCLRB PA1
#define RCLK PA2
#define OEB PA0

#define LED_BOT PB6
#define LED_MID PB7
#define LED_TOP PB8
#define BTN_SET PB5
#define BTN_PLUS PB4
#define BTN_MINUS PB3

// Utility types
enum ButtonState
{
  PRESSED,
  RELEASED,
  CLICKED,
  LONG_PRESSED,
  UNCHANGED
};

ShiftDisplay display(SER, SRCLK, SRCLRB, RCLK, OEB);
ShiftDisplayFSM stateMachine;

char serial_command_buffer_[32];
SerialCommands serial_commands_(&Serial, serial_command_buffer_, sizeof(serial_command_buffer_), "\r\n", " ");
void cmd_unrecognized(SerialCommands *sender, const char *cmd);
void cmd_test(SerialCommands *sender);
void cmd_set_hour(SerialCommands *sender);
void cmd_set_minute(SerialCommands *sender);
void cmd_set_second(SerialCommands *sender);

SerialCommand cmd_test_("TEST", cmd_test);
SerialCommand cmd_set_hour_("SETH", cmd_set_hour);
SerialCommand cmd_set_minute_("SETM", cmd_set_minute);
SerialCommand cmd_set_second_("SETS", cmd_set_second);

STM32RTC &rtc = STM32RTC::getInstance();
DateTimeBuffer_t date_time_buf = {0, 1, RTC_MONTH_JANUARY, 1, 0, 0, 0};
bool time_dirty = true;

AceButton btnSet(BTN_SET);
AceButton btnPlus(BTN_PLUS);
AceButton btnMinus(BTN_MINUS);

ButtonState btnSetState = ButtonState::UNCHANGED;
ButtonState btnPlusState = ButtonState::UNCHANGED;
ButtonState btnMinusState = ButtonState::UNCHANGED;

HardwareTimer *AlrmLEDTim;

uint8_t weekDay;
uint8_t day;
uint8_t month;
uint8_t year;

uint16_t counter = 0;

bool toggling = false;
bool output_en = false;

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
void printSetHourMenu();

// IRQ Handlers and Event Callbacks
void handleEvent(AceButton *, uint8_t, uint8_t);
void irq_rtc_seconds(void *data);
void irq_timer_led();

void setup()
{
  setup_rtc();
  setup_user_btns();
  setup_user_leds();

  display.begin();
  display.enable();

  setup_usb();
  Serial.dtr(true);
  serial_commands_.SetDefaultHandler(cmd_unrecognized);
  serial_commands_.AddCommand(&cmd_test_);
  serial_commands_.AddCommand(&cmd_set_hour_);
  serial_commands_.AddCommand(&cmd_set_minute_);
  serial_commands_.AddCommand(&cmd_set_second_);
  Serial.println("Started");
}

void loop()
{

  // Check buttons
  btnSet.check();
  btnPlus.check();
  btnMinus.check();

  // Check Serial for input
  serial_commands_.ReadSerial();

  // Update display based on current state
  State currentState = stateMachine.getState();
  switch (currentState)
  {
  case State::IDLE:
    if (time_dirty)
    {
      display.clear();
      printClock();
      display.latch();
      time_dirty = false;
    }
    else
    {
      display.update();
    }

    if (btnSetState == ButtonState::CLICKED)
    {
      stateMachine.execute(Action::MENU_ENTER);
    }
    break;

  case State::MENU:
    switch (btnSetState)
    {
    case ButtonState::CLICKED:
      stateMachine.execute(Action::MENU_EXIT);
      break;
    default:
      break;
    }

    printSetHourMenu();
    break;

  default:
    break;
  }

  stateMachine.update();

  // reset the button states
  btnSetState = ButtonState::UNCHANGED;
  btnPlusState = ButtonState::UNCHANGED;
  btnMinusState = ButtonState::UNCHANGED;
}

void setup_user_leds()
{
  pinMode(LED_TOP, OUTPUT);
  pinMode(LED_MID, OUTPUT);
  // pinMode(LED_BOT, OUTPUT);

  digitalWrite(LED_TOP, LOW);
  digitalWrite(LED_MID, LOW);
  // digitalWrite(LED_BOT, LOW);

  // Alarm LED PWM
  TIM_TypeDef *botInstance = (TIM_TypeDef *)pinmap_peripheral(digitalPinToPinName(LED_BOT), PinMap_PWM);
  uint32_t botChannel = STM_PIN_CHANNEL(pinmap_function(digitalPinToPinName(LED_BOT), PinMap_PWM));
  AlrmLEDTim = new HardwareTimer(botInstance);
  AlrmLEDTim->setPWM(botChannel, LED_BOT, 60, 10);
}

void setup_user_btns()
{
  pinMode(BTN_SET, INPUT_PULLUP);
  pinMode(BTN_PLUS, INPUT_PULLUP);
  pinMode(BTN_MINUS, INPUT_PULLUP);

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
  Serial.println(F("Shift Clock Started"));
}

void printClock()
{
  char buffer[6];
  uint8_t seconds = date_time_buf.seconds;
  uint8_t minutes = date_time_buf.minutes;
  uint8_t hours = date_time_buf.hours;

  sprintf(buffer, "%02d%02d%02d", hours, minutes, seconds);
  display.writeDisplay(buffer, 0b00001000);

  // display.shiftOutAscii((seconds % 10) + '0');
  // display.shiftOutAscii((seconds / 10) + '0');

  // display.shiftOutAscii((minutes % 10) + '0', true);
  // display.shiftOutAscii((minutes / 10) + '0');

  // display.shiftOutAscii((hours % 10) + '0');
  // display.shiftOutAscii((hours / 10) + '0');
}

void handleEvent(AceButton *button, uint8_t eventType, uint8_t buttonState)
{
  UNUSED(buttonState);

  if (eventType == AceButton::kEventClicked)
  {
    switch (button->getPin())
    {
    case BTN_SET:
      btnSetState = ButtonState::CLICKED;
      break;
    case BTN_PLUS:
      btnPlusState = ButtonState::CLICKED;
      break;
    case BTN_MINUS:
      btnMinusState = ButtonState::CLICKED;
      break;

    default:
      break;
    }
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

void irq_timer_led()
{
  if (output_en)
  {
    display.enable();
    output_en = false;
  }
  else
  {
    display.disable();
    output_en = true;
  }
}

void printSetHourMenu()
{
  char buffer[6];
  uint8_t hours = date_time_buf.hours;

  sprintf(buffer, "HH  %02d", hours);
  display.writeDisplay(buffer, 0b00000000);
  display.latch();
}

void cmd_unrecognized(SerialCommands *sender, const char *cmd)
{
  sender->GetSerial()->print(F("Unrecognized command ["));
  sender->GetSerial()->print(cmd);
  sender->GetSerial()->println(F("]"));
}

void cmd_test(SerialCommands *sender)
{
  Serial.println("TEST");
}

void cmd_set_hour(SerialCommands *sender)
{
  char *hour_str = sender->Next();
  if (hour_str == NULL)
  {
    sender->GetSerial()->println("ERROR NO_HOUR");
    return;
  }

  int hour = atoi(hour_str);
  if (hour < 0 || hour >= 24)
  {
    sender->GetSerial()->printf("ERROR HOUR OUT OF RANGE {%d}: %s", hour, hour_str);
    return;
  }

  date_time_buf.hours = hour;
  rtc.setHours(hour);
}

void cmd_set_minute(SerialCommands *sender)
{
  char *m_str = sender->Next();
  if (m_str == NULL)
  {
    sender->GetSerial()->println("ERROR NO_MINUTE");
    return;
  }

  int m = atoi(m_str);
  if (m < 0 || m >= 60)
  {
    sender->GetSerial()->printf("ERROR MINUTE OUT OF RANGE {%d}: %s", m, m_str);
    return;
  }

  date_time_buf.minutes = m;
  rtc.setMinutes(m);
}

void cmd_set_second(SerialCommands *sender)
{
  char *second_str = sender->Next();
  if (second_str == NULL)
  {
    sender->GetSerial()->println("ERROR NO_SECOND");
    return;
  }

  int second = atoi(second_str);
  if (second < 0 || second >= 60)
  {
    sender->GetSerial()->printf("ERROR SECOND OUT OF RANGE {%d}: %s", second, second_str);
    return;
  }

  date_time_buf.seconds = second;
  rtc.setSeconds(second);
}
