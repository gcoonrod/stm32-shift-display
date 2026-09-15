#include <Arduino.h>
#include <ShiftDisplay.h>
#include <ShiftDisplayFSM.h>
#include <STM32RTC.h>
#include <AceButton.h>
#include <SerialCommands.h>
#include <time.h>
#include <stdlib.h>
#include "header.h"

#define SERIAL_COMMANDS_DEBUG

using namespace ace_button;

#define SER PA3
#define SRCLK PA4
#define SRCLRB PA1
#define RCLK PA2
#define OEB PA0

/**
 * User LEDs. The positional names below are how the LEDs sit on the board; note
 * that they run opposite to the schematic's D-numbering, which is a trap worth
 * keeping in mind when cross-referencing:
 *
 *   LED_TOP  PB8  net /LED3  schematic D3  -> AM indicator (12-hour mode only)
 *   LED_MID  PB7  net /LED2  schematic D2  -> 12-hour mode active
 *   LED_BOT  PB6  net /LED1  schematic D1  -> alarm armed (steady) / firing (blink)
 *
 * All three are active high: the cathodes tie to GND through their resistors.
 */
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
  REPEATED,
  UNCHANGED
};

ShiftDisplay display(SER, SRCLK, SRCLRB, RCLK, OEB);
ShiftDisplayFSM stateMachine;

char serial_command_buffer_[32];
SerialCommands serial_commands_(&Serial, serial_command_buffer_, sizeof(serial_command_buffer_), "\r\n", " ");
void cmd_unrecognized(SerialCommands *sender, const char *cmd);
void cmd_test(SerialCommands *sender);
void cmd_set_time(SerialCommands *sender);
void cmd_get_time(SerialCommands *sender);
void cmd_set_offset(SerialCommands *sender);
void cmd_get_offset(SerialCommands *sender);
void cmd_get_mode(SerialCommands *sender);
void cmd_set_mode(SerialCommands *sender);
void cmd_get_alarm(SerialCommands *sender);
void cmd_set_alarm(SerialCommands *sender);
void cmd_alarm_enable(SerialCommands *sender);
void cmd_get_leds(SerialCommands *sender);

SerialCommand cmd_test_("TEST", cmd_test);
SerialCommand cmd_set_time_("ST", cmd_set_time);
SerialCommand cmd_get_time_("GT", cmd_get_time);
SerialCommand cmd_set_offset_("SO", cmd_set_offset);
SerialCommand cmd_get_offset_("GO", cmd_get_offset);
SerialCommand cmd_get_mode_("GM", cmd_get_mode);
SerialCommand cmd_set_mode_("SM", cmd_set_mode);
SerialCommand cmd_get_alarm_("GA", cmd_get_alarm);
SerialCommand cmd_set_alarm_("SA", cmd_set_alarm);
SerialCommand cmd_alarm_enable_("AE", cmd_alarm_enable);
SerialCommand cmd_get_leds_("GL", cmd_get_leds);

STM32RTC &rtc = STM32RTC::getInstance();
DateTimeBuffer_t date_time_buf = {0, 1, RTC_MONTH_JANUARY, 1, 0, 0, 0};
bool time_dirty = true;
int8_t timezoneOffset = -6; // CST

/**
 * Settings that outlive a power cut, held in the backup domain on VBAT.
 *
 * Register budget on the F103C8 is DR1..DR10, and most of it is already spoken
 * for: the core claims DR1 (RTC_BKP_INDEX), DR4 (HID_MAGIC_NUMBER_BKP_INDEX) and
 * DR10 in backup.h, and STM32RTC stores the F1's emulated date across DR6 and
 * DR7 (RTC_BKP_DATE). Writing any of those would corrupt someone else's state --
 * the date one in particular would surface as a clock bug, not a settings bug.
 * DR2, DR3 and DR5 are free.
 */
#define BKP_MAGIC_REG LL_RTC_BKP_DR2
#define BKP_FLAGS_REG LL_RTC_BKP_DR3
#define BKP_ALARM_REG LL_RTC_BKP_DR5
#define BKP_MAGIC_VALUE 0xC10CU

#define FLAG_MODE_12 0x1U
#define FLAG_ALARM_ARMED 0x2U

typedef struct
{
  bool mode12;
  bool alarmArmed;
  uint8_t alarmHour;
  uint8_t alarmMinute;
} Settings_t;

Settings_t settings = {false, false, 7, 0};

// SET gets its own config; see setup_user_btns() for why it must not share
// the repeat-press feature with PLUS/MINUS.
ButtonConfig setButtonConfig;
AceButton btnSet(BTN_SET);
AceButton btnPlus(BTN_PLUS);
AceButton btnMinus(BTN_MINUS);

ButtonState btnSetState = ButtonState::UNCHANGED;
ButtonState btnPlusState = ButtonState::UNCHANGED;
ButtonState btnMinusState = ButtonState::UNCHANGED;

// Values being edited. Copied in when an editor is entered and only written
// through on commit, so backing out discards them.
uint8_t edit_field[3] = {0, 0, 0};
MenuState edit_item = MENU_NONE;

volatile bool alarm_fire_request = false;

HardwareTimer *AlrmLEDTim;
bool output_en = false;

uint32_t last_activity_ms = 0;

// What was last shifted out, so the display is only re-latched when it changes.
char last_rendered[7] = {0};
uint8_t last_rendered_dp = 0xFF;

#define BLINK_PERIOD_MS 300
#define MENU_TIMEOUT_MS 10000

// Function definitions
void setup_user_leds();
void setup_user_btns();
void setup_rtc();
void setup_usb();
void settings_load();
void settings_save();
void update_leds();
void render();
void handle_input();
void begin_edit();
void adjust_field(int8_t delta);
void commit_edit();

// IRQ Handlers and Event Callbacks
void handleEvent(AceButton *, uint8_t, uint8_t);
void irq_rtc_seconds(void *data);
void irq_timer_led();

static inline bool blink_on()
{
  return ((millis() / BLINK_PERIOD_MS) % 2) == 0;
}

void setup()
{
  setup_rtc();
  setup_user_btns();
  setup_user_leds();

  settings_load();

  display.begin();
  display.enable();

  setup_usb();
  Serial.dtr(true);
  Serial.begin();
  serial_commands_.SetDefaultHandler(cmd_unrecognized);
  serial_commands_.AddCommand(&cmd_test_);
  serial_commands_.AddCommand(&cmd_set_time_);
  serial_commands_.AddCommand(&cmd_get_time_);
  serial_commands_.AddCommand(&cmd_set_offset_);
  serial_commands_.AddCommand(&cmd_get_offset_);
  serial_commands_.AddCommand(&cmd_get_mode_);
  serial_commands_.AddCommand(&cmd_set_mode_);
  serial_commands_.AddCommand(&cmd_get_alarm_);
  serial_commands_.AddCommand(&cmd_set_alarm_);
  serial_commands_.AddCommand(&cmd_alarm_enable_);
  serial_commands_.AddCommand(&cmd_get_leds_);

  last_activity_ms = millis();

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

  if (alarm_fire_request)
  {
    alarm_fire_request = false;
    stateMachine.execute(Action::ALARM_FIRE);
  }

  handle_input();

  // Nothing pressed for a while: drop back to the clock rather than leaving the
  // device stranded in a submenu. Uncommitted edits are discarded.
  if (stateMachine.getState() != State::IDLE &&
      stateMachine.getState() != State::FIRING &&
      (millis() - last_activity_ms) > MENU_TIMEOUT_MS)
  {
    stateMachine.execute(Action::MENU_TIMEOUT);
  }

  stateMachine.update();

  if (stateMachine.takeCommit())
  {
    commit_edit();
  }

  render();
  update_leds();

  // reset the button states
  btnSetState = ButtonState::UNCHANGED;
  btnPlusState = ButtonState::UNCHANGED;
  btnMinusState = ButtonState::UNCHANGED;
}

void setup_user_leds()
{
  pinMode(LED_TOP, OUTPUT);
  pinMode(LED_MID, OUTPUT);
  pinMode(LED_BOT, OUTPUT);

  digitalWrite(LED_TOP, LOW);
  digitalWrite(LED_MID, LOW);
  digitalWrite(LED_BOT, LOW);

  // Alarm LED PWM
  // TIM_TypeDef *botInstance = (TIM_TypeDef *)pinmap_peripheral(digitalPinToPinName(LED_BOT), PinMap_PWM);
  // uint32_t botChannel = STM_PIN_CHANNEL(pinmap_function(digitalPinToPinName(LED_BOT), PinMap_PWM));
  // AlrmLEDTim = new HardwareTimer(botInstance);
  // AlrmLEDTim->setPWM(botChannel, LED_BOT, 60, 10);
}

void setup_user_btns()
{
  pinMode(BTN_SET, INPUT_PULLUP);
  pinMode(BTN_PLUS, INPUT_PULLUP);
  pinMode(BTN_MINUS, INPUT_PULLUP);

  /**
   * PLUS and MINUS: click, and hold-to-repeat while adjusting a field.
   *
   * SET: click, and long press to back out of a level. It needs a separate
   * config because AceButton::check() evaluates long-press and repeat-press in
   * the same pass, and kLongPressDelay and kRepeatPressDelay are both 1000 ms
   * by default. Sharing one config meant a held SET emitted kEventLongPressed
   * and then kEventRepeatPressed immediately after, the latter overwriting the
   * former in the button-state latch -- so holding SET did nothing at all.
   * Keeping repeat off SET removes the collision at its source.
   *
   * kFeatureDoubleClick is deliberately not enabled anywhere: nothing uses
   * double-click and it delays every click by the double-click window.
   */
  ButtonConfig *adjustConfig = ButtonConfig::getSystemButtonConfig();
  adjustConfig->setEventHandler(handleEvent);
  adjustConfig->setFeature(ButtonConfig::kFeatureClick);
  adjustConfig->setFeature(ButtonConfig::kFeatureRepeatPress);

  setButtonConfig.setEventHandler(handleEvent);
  setButtonConfig.setFeature(ButtonConfig::kFeatureClick);
  setButtonConfig.setFeature(ButtonConfig::kFeatureLongPress);
  // Without this, releasing after a long press would also emit a click and
  // advance the field the user was trying to leave.
  setButtonConfig.setFeature(ButtonConfig::kFeatureSuppressAfterLongPress);
  btnSet.setButtonConfig(&setButtonConfig);
}

void setup_rtc()
{
  rtc.setClockSource(STM32RTC::LSE_CLOCK);
  delay(500);
  rtc.begin(false, STM32RTC::HOUR_24);

  // get date from rtc backup
  rtc.getDate(&date_time_buf.week_day, &date_time_buf.day, &date_time_buf.month, &date_time_buf.year);

  // if it is the epoch we need to reset, otherwise carry on.
  if ((date_time_buf.day == 1) && (date_time_buf.month == RTC_MONTH_JANUARY) && (date_time_buf.year == 1))
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

void settings_load()
{
  enableBackupDomain();

  if (getBackupRegister(BKP_MAGIC_REG) != BKP_MAGIC_VALUE)
  {
    // Backup memory has never held settings (fresh coin cell, or first run of
    // this firmware). Without the magic, arbitrary contents would read as a
    // valid configuration.
    settings_save();
    return;
  }

  uint32_t flags = getBackupRegister(BKP_FLAGS_REG);
  settings.mode12 = (flags & FLAG_MODE_12) != 0;
  settings.alarmArmed = (flags & FLAG_ALARM_ARMED) != 0;

  uint32_t alarm = getBackupRegister(BKP_ALARM_REG);
  settings.alarmHour = (alarm >> 8) & 0xFF;
  settings.alarmMinute = alarm & 0xFF;

  if (settings.alarmHour > 23)
  {
    settings.alarmHour = 0;
  }
  if (settings.alarmMinute > 59)
  {
    settings.alarmMinute = 0;
  }
}

void settings_save()
{
  enableBackupDomain();

  uint32_t flags = 0;
  if (settings.mode12)
  {
    flags |= FLAG_MODE_12;
  }
  if (settings.alarmArmed)
  {
    flags |= FLAG_ALARM_ARMED;
  }

  setBackupRegister(BKP_FLAGS_REG, flags);
  setBackupRegister(BKP_ALARM_REG, ((uint32_t)settings.alarmHour << 8) | settings.alarmMinute);
  setBackupRegister(BKP_MAGIC_REG, BKP_MAGIC_VALUE);
}

// Hours as they should be shown. The RTC stays in HOUR_24 whatever the display
// mode is, so the serial commands keep reading and writing the same values.
static uint8_t display_hours(uint8_t hours24)
{
  if (!settings.mode12)
  {
    return hours24;
  }

  uint8_t h = hours24 % 12;
  return (h == 0) ? 12 : h;
}

void update_leds()
{
  bool am = settings.mode12 && (date_time_buf.hours < 12);
  digitalWrite(LED_TOP, am ? HIGH : LOW);
  digitalWrite(LED_MID, settings.mode12 ? HIGH : LOW);

  bool alarmLed = (stateMachine.getState() == State::FIRING)
                      ? blink_on()
                      : settings.alarmArmed;
  digitalWrite(LED_BOT, alarmLed ? HIGH : LOW);
}

// Write a two-digit value at pos, or blank it out when it should be hidden.
static void put2(char *buf, uint8_t pos, uint8_t value, bool visible)
{
  if (visible)
  {
    buf[pos] = '0' + ((value / 10) % 10);
    buf[pos + 1] = '0' + (value % 10);
  }
  else
  {
    buf[pos] = ' ';
    buf[pos + 1] = ' ';
  }
}

static void render_menu(char *buf)
{
  const char *label;
  switch (stateMachine.getMenuState())
  {
  case MENU_TIME:
    label = " CLOC ";
    break;
  case MENU_DATE:
    label = " DAtE ";
    break;
  case MENU_MODE:
    label = " 12-24";
    break;
  case MENU_ALARM:
    label = " ALArn";
    break;
  default:
    label = "      ";
    break;
  }
  memcpy(buf, label, 6);
}

static void render_edit(char *buf)
{
  uint8_t field = stateMachine.getField();
  bool show = blink_on();

  memcpy(buf, "      ", 6);

  switch (edit_item)
  {
  case MENU_TIME:
    put2(buf, 0, edit_field[0], (field != 0) || show);
    put2(buf, 2, edit_field[1], (field != 1) || show);
    put2(buf, 4, edit_field[2], (field != 2) || show);
    break;

  case MENU_DATE:
    put2(buf, 0, edit_field[0], (field != 0) || show);
    put2(buf, 2, edit_field[1], (field != 1) || show);
    put2(buf, 4, edit_field[2], (field != 2) || show);
    break;

  case MENU_MODE:
    if (show)
    {
      memcpy(buf, edit_field[0] ? "  12Hr" : "  24Hr", 6);
    }
    break;

  case MENU_ALARM:
    if (field < 2)
    {
      put2(buf, 0, edit_field[0], (field != 0) || show);
      put2(buf, 2, edit_field[1], (field != 1) || show);
    }
    else if (show)
    {
      memcpy(buf, edit_field[2] ? "    On" : "   OFF", 6);
    }
    break;

  default:
    break;
  }
}

void render()
{
  char buf[7];
  uint8_t dp = 0;

  buf[6] = '\0';

  switch (stateMachine.getState())
  {
  case State::MENU:
    render_menu(buf);
    break;

  case State::EDIT:
    render_edit(buf);
    break;

  case State::FIRING:
    // Flash the whole display so a firing alarm cannot be mistaken for the clock.
    if (blink_on())
    {
      put2(buf, 0, display_hours(date_time_buf.hours), true);
      put2(buf, 2, date_time_buf.minutes, true);
      put2(buf, 4, date_time_buf.seconds, true);
      dp = 0b00001000;
    }
    else
    {
      memcpy(buf, "      ", 6);
    }
    break;

  case State::IDLE:
  default:
    put2(buf, 0, display_hours(date_time_buf.hours), true);
    put2(buf, 2, date_time_buf.minutes, true);
    put2(buf, 4, date_time_buf.seconds, true);
    dp = 0b00001000;
    time_dirty = false;
    break;
  }

  // Only re-shift and latch when something actually changed; the idle path
  // would otherwise re-shift six characters every pass of the loop.
  if (dp != last_rendered_dp || memcmp(buf, last_rendered, 6) != 0)
  {
    display.writeDisplay(buf, dp);
    display.latch();
    memcpy(last_rendered, buf, 6);
    last_rendered[6] = '\0';
    last_rendered_dp = dp;
  }
}

void begin_edit()
{
  edit_item = stateMachine.getMenuState();

  switch (edit_item)
  {
  case MENU_TIME:
    edit_field[0] = date_time_buf.hours;
    edit_field[1] = date_time_buf.minutes;
    edit_field[2] = date_time_buf.seconds;
    break;

  case MENU_DATE:
    edit_field[0] = date_time_buf.day;
    edit_field[1] = date_time_buf.month;
    edit_field[2] = date_time_buf.year;
    break;

  case MENU_MODE:
    edit_field[0] = settings.mode12 ? 1 : 0;
    break;

  case MENU_ALARM:
    edit_field[0] = settings.alarmHour;
    edit_field[1] = settings.alarmMinute;
    edit_field[2] = settings.alarmArmed ? 1 : 0;
    break;

  default:
    break;
  }
}

// Inclusive limits for the field currently being edited.
static void field_limits(uint8_t field, uint8_t *lo, uint8_t *hi)
{
  *lo = 0;
  *hi = 0;

  switch (edit_item)
  {
  case MENU_TIME:
    *hi = (field == 0) ? 23 : 59;
    break;

  case MENU_DATE:
    if (field == 0)
    {
      *lo = 1;
      *hi = 31;
    }
    else if (field == 1)
    {
      *lo = 1;
      *hi = 12;
    }
    else
    {
      *hi = 99;
    }
    break;

  case MENU_MODE:
    *hi = 1;
    break;

  case MENU_ALARM:
    if (field == 0)
    {
      *hi = 23;
    }
    else if (field == 1)
    {
      *hi = 59;
    }
    else
    {
      *hi = 1;
    }
    break;

  default:
    break;
  }
}

void adjust_field(int8_t delta)
{
  uint8_t field = stateMachine.getField();
  if (field >= 3)
  {
    return;
  }

  uint8_t lo, hi;
  field_limits(field, &lo, &hi);
  if (hi <= lo)
  {
    return; // nothing adjustable for this field
  }

  int16_t value = (int16_t)edit_field[field] + delta;
  if (value > (int16_t)hi)
  {
    value = lo;
  }
  else if (value < (int16_t)lo)
  {
    value = hi;
  }

  edit_field[field] = (uint8_t)value;
}

void commit_edit()
{
  switch (edit_item)
  {
  case MENU_TIME:
    rtc.setHours(edit_field[0]);
    rtc.setMinutes(edit_field[1]);
    rtc.setSeconds(edit_field[2]);
    date_time_buf.hours = edit_field[0];
    date_time_buf.minutes = edit_field[1];
    date_time_buf.seconds = edit_field[2];
    break;

  case MENU_DATE:
    rtc.setDay(edit_field[0]);
    rtc.setMonth(edit_field[1]);
    rtc.setYear(edit_field[2]);
    date_time_buf.day = edit_field[0];
    date_time_buf.month = edit_field[1];
    date_time_buf.year = edit_field[2];
    break;

  case MENU_MODE:
    settings.mode12 = (edit_field[0] != 0);
    settings_save();
    break;

  case MENU_ALARM:
    settings.alarmHour = edit_field[0];
    settings.alarmMinute = edit_field[1];
    settings.alarmArmed = (edit_field[2] != 0);
    settings_save();
    break;

  default:
    break;
  }

  edit_item = MENU_NONE;
}

void handle_input()
{
  bool anyButton = (btnSetState != ButtonState::UNCHANGED) ||
                   (btnPlusState != ButtonState::UNCHANGED) ||
                   (btnMinusState != ButtonState::UNCHANGED);

  if (anyButton)
  {
    last_activity_ms = millis();
  }

  // A firing alarm swallows the press that silences it, so dismissing does not
  // also drop the user into the menu.
  if (stateMachine.getState() == State::FIRING)
  {
    if (anyButton)
    {
      stateMachine.execute(Action::ALARM_DISMISS);
    }
    return;
  }

  switch (stateMachine.getState())
  {
  case State::IDLE:
    if (btnSetState == ButtonState::CLICKED)
    {
      stateMachine.execute(Action::MENU_ENTER);
    }
    break;

  case State::MENU:
    if (btnSetState == ButtonState::LONG_PRESSED)
    {
      stateMachine.execute(Action::MENU_EXIT);
    }
    else if (btnSetState == ButtonState::CLICKED)
    {
      begin_edit();
      stateMachine.execute(Action::MENU_SELECT);
    }
    else if (btnPlusState == ButtonState::CLICKED || btnPlusState == ButtonState::REPEATED)
    {
      stateMachine.execute(Action::MENU_UP);
    }
    else if (btnMinusState == ButtonState::CLICKED || btnMinusState == ButtonState::REPEATED)
    {
      stateMachine.execute(Action::MENU_DOWN);
    }
    break;

  case State::EDIT:
    if (btnSetState == ButtonState::LONG_PRESSED)
    {
      stateMachine.execute(Action::MENU_EXIT);
    }
    else if (btnSetState == ButtonState::CLICKED)
    {
      stateMachine.execute(Action::EDIT_NEXT);
    }
    else if (btnPlusState == ButtonState::CLICKED || btnPlusState == ButtonState::REPEATED)
    {
      adjust_field(1);
    }
    else if (btnMinusState == ButtonState::CLICKED || btnMinusState == ButtonState::REPEATED)
    {
      adjust_field(-1);
    }
    break;

  default:
    break;
  }
}

void handleEvent(AceButton *button, uint8_t eventType, uint8_t buttonState)
{
  UNUSED(buttonState);

  ButtonState mapped;
  switch (eventType)
  {
  case AceButton::kEventClicked:
    mapped = ButtonState::CLICKED;
    break;
  case AceButton::kEventLongPressed:
    mapped = ButtonState::LONG_PRESSED;
    break;
  case AceButton::kEventRepeatPressed:
    mapped = ButtonState::REPEATED;
    break;
  default:
    return;
  }

  ButtonState *slot;
  switch (button->getPin())
  {
  case BTN_SET:
    slot = &btnSetState;
    break;
  case BTN_PLUS:
    slot = &btnPlusState;
    break;
  case BTN_MINUS:
    slot = &btnMinusState;
    break;

  default:
    return;
  }

  // AceButton can emit more than one event for a button in a single check(),
  // and the loop only consumes one per pass. A long press is the intent the
  // user waited for, so it is never discarded by whatever follows it.
  if (*slot == ButtonState::LONG_PRESSED && mapped != ButtonState::LONG_PRESSED)
  {
    return;
  }

  *slot = mapped;
}

// Unused: the hook intended for PWM brightness control on the 595 OE line.
// Nothing calls this yet -- see docs/DEVELOPMENT.md, "Unimplemented".
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

void irq_rtc_seconds(void *data)
{
  UNUSED(data);

  // Update the clock buffer
  date_time_buf.year = rtc.getYear();
  date_time_buf.month = rtc.getMonth();
  date_time_buf.day = rtc.getDay();
  date_time_buf.week_day = rtc.getWeekDay();
  date_time_buf.hours = rtc.getHours();
  date_time_buf.minutes = rtc.getMinutes();
  date_time_buf.seconds = rtc.getSeconds();

  time_dirty = true;

  // Daily alarm. Matching on seconds == 0 means this is true for exactly one
  // tick per day, so it fires once and re-arms itself simply by the clock
  // coming round again -- no RTC alarm peripheral, and nothing to reschedule.
  if (settings.alarmArmed &&
      date_time_buf.seconds == 0 &&
      date_time_buf.hours == settings.alarmHour &&
      date_time_buf.minutes == settings.alarmMinute)
  {
    alarm_fire_request = true;
  }
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

tm* unixTimestampToTime(const char *timestampStr) {
  // Convert the timestamp string to a time_t object
  time_t timestamp = atol(timestampStr); 

  timestamp += timezoneOffset * 60 * 60;

  // Use localtime to convert the timestamp to a tm struct
  return localtime(&timestamp); 
}

void cmd_set_time(SerialCommands *sender)
{
  char *timestampStr = sender->Next();
  if (timestampStr == NULL)
  {
    sender->GetSerial()->println("ERROR NO_TIMESTAMP");
    return;
  }

  tm *time = unixTimestampToTime(timestampStr);

  date_time_buf.hours = time->tm_hour;
  date_time_buf.minutes = time->tm_min;
  date_time_buf.seconds = time->tm_sec;
  date_time_buf.day = time->tm_mday;
  date_time_buf.month = time->tm_mon;
  date_time_buf.year = time->tm_year - 100;

  rtc.setHours(time->tm_hour);
  rtc.setMinutes(time->tm_min);
  rtc.setSeconds(time->tm_sec);
  rtc.setDay(time->tm_mday);
  rtc.setMonth(time->tm_mon);
  rtc.setYear(time->tm_year - 100);

  sender->GetSerial()->println("OK");
}

void cmd_get_time(SerialCommands *sender)
{

  // convert the date_time_buf to tm struct
  tm time = {0};
  time.tm_hour = date_time_buf.hours;
  time.tm_min = date_time_buf.minutes;
  time.tm_sec = date_time_buf.seconds;
  time.tm_mday = date_time_buf.day;
  time.tm_mon = date_time_buf.month;
  time.tm_year = date_time_buf.year + 100;

  // Apply the timezone offset to change the time to UTC
  time.tm_sec -= timezoneOffset * 60 * 60;

  time_t timestamp = mktime(&time);

  // Return the current time as a Unix timestamp
  sender->GetSerial()->println(timestamp);
}

void cmd_set_offset(SerialCommands *sender)
{
  char *offsetStr = sender->Next();
  if (offsetStr == NULL)
  {
    sender->GetSerial()->println("ERROR NO_OFFSET");
    return;
  }

  int offset = atoi(offsetStr);
  timezoneOffset = offset;
  sender->GetSerial()->println("OK");
}

void cmd_get_offset(SerialCommands *sender)
{
  sender->GetSerial()->println(timezoneOffset);
}
void cmd_get_mode(SerialCommands *sender)
{
  sender->GetSerial()->println(settings.mode12 ? 12 : 24);
}

void cmd_set_mode(SerialCommands *sender)
{
  char *mode_str = sender->Next();
  if (mode_str == NULL)
  {
    sender->GetSerial()->println("ERROR NO_MODE");
    return;
  }

  int mode = atoi(mode_str);
  if (mode != 12 && mode != 24)
  {
    sender->GetSerial()->printf("ERROR MODE OUT OF RANGE {%d}: %s", mode, mode_str);
    return;
  }

  settings.mode12 = (mode == 12);
  settings_save();
  sender->GetSerial()->println("OK");
}

void cmd_get_alarm(SerialCommands *sender)
{
  // "<hh> <mm> <armed>"
  sender->GetSerial()->printf("%02d %02d %d\r\n",
                              settings.alarmHour,
                              settings.alarmMinute,
                              settings.alarmArmed ? 1 : 0);
}

void cmd_set_alarm(SerialCommands *sender)
{
  char *hour_str = sender->Next();
  if (hour_str == NULL)
  {
    sender->GetSerial()->println("ERROR NO_HOUR");
    return;
  }

  char *minute_str = sender->Next();
  if (minute_str == NULL)
  {
    sender->GetSerial()->println("ERROR NO_MINUTE");
    return;
  }

  int hour = atoi(hour_str);
  if (hour < 0 || hour >= 24)
  {
    sender->GetSerial()->printf("ERROR HOUR OUT OF RANGE {%d}: %s", hour, hour_str);
    return;
  }

  int minute = atoi(minute_str);
  if (minute < 0 || minute >= 60)
  {
    sender->GetSerial()->printf("ERROR MINUTE OUT OF RANGE {%d}: %s", minute, minute_str);
    return;
  }

  settings.alarmHour = hour;
  settings.alarmMinute = minute;
  settings_save();
  sender->GetSerial()->println("OK");
}

void cmd_alarm_enable(SerialCommands *sender)
{
  char *enable_str = sender->Next();
  if (enable_str == NULL)
  {
    sender->GetSerial()->println("ERROR NO_STATE");
    return;
  }

  int enable = atoi(enable_str);
  if (enable != 0 && enable != 1)
  {
    sender->GetSerial()->printf("ERROR STATE OUT OF RANGE {%d}: %s", enable, enable_str);
    return;
  }

  settings.alarmArmed = (enable == 1);
  settings_save();

  // Disarming silences a firing alarm too. Without this the only way out of the
  // firing state is a button press, which leaves no way to stop it from a host.
  if (!settings.alarmArmed && stateMachine.getState() == State::FIRING)
  {
    stateMachine.execute(Action::ALARM_DISMISS);
  }

  sender->GetSerial()->println("OK");
}

void cmd_get_leds(SerialCommands *sender)
{
  // "<top> <mid> <bot>" -- lets the indicators be checked from the host
  // instead of only by eye.
  sender->GetSerial()->printf("%d %d %d\r\n",
                              digitalRead(LED_TOP) == HIGH ? 1 : 0,
                              digitalRead(LED_MID) == HIGH ? 1 : 0,
                              digitalRead(LED_BOT) == HIGH ? 1 : 0);
}
