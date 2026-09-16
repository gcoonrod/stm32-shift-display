#include <Arduino.h>
#include <ShiftDisplay.h>
#include <ShiftDisplayFSM.h>
#include <STM32RTC.h>
#include <AceButton.h>
#include <SerialCommands.h>
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
 * ~OE must not be one of the shift-register control pins, and none of them may
 * alias another.
 *
 * ~OE shares GPIOA with all four, and the driver reaches its pins through BSRR
 * words composed from their masks. If a pin define were ever edited to collide
 * with OEB, those words would reach across the brightness timer's pin -- and
 * once the display engine replays them from a DMA buffer there is nothing in
 * the instruction stream to show it. The failure would look like a display that
 * dims wrongly, not like a bug.
 *
 * This catches an alias at build time. ShiftDisplay::begin() catches the subtler
 * case of two distinct pin numbers landing on the same port bit, which only the
 * variant's pin map knows.
 */
static_assert(OEB != SER && OEB != SRCLK && OEB != SRCLRB && OEB != RCLK,
              "The output-enable pin collides with a shift-register control pin");
static_assert(SER != SRCLK && SER != SRCLRB && SER != RCLK &&
                  SRCLK != SRCLRB && SRCLK != RCLK && SRCLRB != RCLK,
              "Two shift-register control pins are assigned to the same pin");

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
void cmd_get_bright(SerialCommands *sender);
void cmd_set_bright(SerialCommands *sender);
void cmd_get_disp(SerialCommands *sender);
void cmd_get_backup(SerialCommands *sender);
#ifdef SHIFT_SWEEP
void cmd_set_nops(SerialCommands *sender);
void cmd_set_pattern(SerialCommands *sender);
#endif
#ifdef SHIFT_ENGINE_VERIFY
void cmd_wave_verify(SerialCommands *sender);
void cmd_wave_level(SerialCommands *sender);
void cmd_wave_dump(SerialCommands *sender);
void cmd_wave_rate(SerialCommands *sender);
void cmd_wave_engine(SerialCommands *sender);
void cmd_wave_block(SerialCommands *sender);
void cmd_wave_reset(SerialCommands *sender);
void cmd_wave_tear(SerialCommands *sender);
void cmd_wave_fade(SerialCommands *sender);
#endif
void cmd_set_disp(SerialCommands *sender);

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
SerialCommand cmd_get_bright_("GI", cmd_get_bright);
SerialCommand cmd_set_bright_("SI", cmd_set_bright);
SerialCommand cmd_get_disp_("GD", cmd_get_disp);
SerialCommand cmd_set_disp_("SD", cmd_set_disp);
SerialCommand cmd_get_backup_("GB", cmd_get_backup);
#ifdef SHIFT_SWEEP
SerialCommand cmd_set_nops_("SN", cmd_set_nops);
SerialCommand cmd_set_pattern_("TP", cmd_set_pattern);
#endif
#ifdef SHIFT_ENGINE_VERIFY
/* Replays the waveform from the CPU, writing exactly the words and in exactly
   the order the two DMA channels will. Establishes that the buffer is right
   before any peripheral is configured -- debugging a DMA display by eye is hard
   enough without also wondering whether what it replays was ever correct. */
bool wave_verify = false;
SerialCommand cmd_wave_verify_("WV", cmd_wave_verify);
SerialCommand cmd_wave_level_("WL", cmd_wave_level);
SerialCommand cmd_wave_dump_("WD", cmd_wave_dump);
SerialCommand cmd_wave_rate_("WR", cmd_wave_rate);
SerialCommand cmd_wave_engine_("WE", cmd_wave_engine);
SerialCommand cmd_wave_block_("WB", cmd_wave_block);
SerialCommand cmd_wave_reset_("WX", cmd_wave_reset);
SerialCommand cmd_wave_tear_("WT", cmd_wave_tear);
SerialCommand cmd_wave_fade_("WF", cmd_wave_fade);
#endif

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
/* Low byte: indicator level, stored as-is. High byte: display level, stored
   offset by one so that zero means "never written".
   
   The low byte shipped first, and the high byte was left as zero. Storing the
   display level raw would make that zero indistinguishable from a deliberate
   level 0 -- the dimmest setting -- so every device already in the field would
   come up looking blank on the first boot after this change. The offset costs a
   byte of arithmetic and avoids resetting the settings that are already there. */
#define BKP_BRIGHT_REG LL_RTC_BKP_DR8
#define BKP_MAGIC_VALUE 0xC10CU

#define FLAG_MODE_12 0x1U
#define FLAG_ALARM_ARMED 0x2U

typedef struct
{
  bool mode12;
  bool alarmArmed;
  uint8_t alarmHour;
  uint8_t alarmMinute;
  uint8_t indicatorLevel; // index into led_gamma
  uint8_t displayLevel;   // index into disp_gamma
} Settings_t;

Settings_t settings = {false, false, 7, 0, 4, 5};

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

#ifdef SHIFT_SWEEP
/**
 * Measurement scaffolding, compiled only into the sweep build.
 *
 * The shift rate has to be established on the assembled board: the parts are
 * 74HC at 3.3 V and the chain is six deep, and the far end is not wired back, so
 * there is no electrical readback. The only instrument is the display itself,
 * which means the test pattern has to make corruption unmistakable.
 *
 * Two patterns, because there are two failure modes. All segments lit exposes a
 * dropped bit. Six distinct digits expose a stream that has shifted -- which the
 * all-lit pattern would hide completely, every digit being identical.
 */
extern volatile uint8_t shift_edge_nops;
uint8_t test_pattern = 0; // 0 = show the clock, 1 = 888888, 2 = 012345
#endif

// What the very first backup-register read returned at boot, captured before
// anything can overwrite it. If settings_load() misreads the magic on a cold
// start it rewrites defaults over the saved values, which destroys them
// permanently -- this is how to tell that apart from the registers themselves
// having been cleared.
uint32_t boot_magic_seen = 0xFFFFFFFFU;
uint32_t boot_flags_seen = 0xFFFFFFFFU;
uint32_t boot_alarm_seen = 0xFFFFFFFFU;
uint32_t boot_bright_seen = 0xFFFFFFFFU;

bool output_en = false;

uint32_t last_activity_ms = 0;

// What was last shifted out, so the display is only re-latched when it changes.
char last_rendered[7] = {0};
uint8_t last_rendered_dp = 0xFF;

#define BLINK_PERIOD_MS 300
#define BREATH_PERIOD_MS 2500

/* PWM_BITS, PWM_MAX_DUTY and PWM_FREQ_HZ moved to header.h: the display engine's
   timer constants are derived from PWM_FREQ_HZ, so it has to be visible to the
   ShiftDisplay library as well as to this file. */

/**
 * Indicator brightness levels, mapped to duty on a gamma curve. Luminous output
 * is linear in duty but perceived lightness is not -- roughly luminance^(1/2.2) --
 * so evenly spaced duty values feel bunched at the bottom and flat at the top.
 * A table beats pow() on a core with no FPU, and at eight entries it costs eight
 * bytes. The lowest entry is floored above the curve's own value so the dimmest
 * setting stays visible rather than reading as a failed LED.
 */
#define LED_LEVELS 8
static const uint16_t led_gamma[LED_LEVELS] = {64, 194, 473, 891, 1456, 2175, 3053, 4095};

/* The same curve for the display, with a higher floor: an indicator only has to
   be visible at its dimmest, whereas the display has to stay readable. */
#define DISP_LEVELS 8
static const uint16_t disp_gamma[DISP_LEVELS] = {128, 259, 546, 964, 1529, 2248, 3126, 4095};
#define MENU_TIMEOUT_MS 10000

// Function definitions
void setup_user_leds();
void setup_user_btns();
void setup_rtc();
void setup_usb();
void settings_load();
void settings_save();
void update_leds();
void update_display_brightness();
void render();
void handle_input();
void begin_edit();
void adjust_field(int8_t delta);
void commit_edit();

// IRQ Handlers and Event Callbacks
void handleEvent(AceButton *, uint8_t, uint8_t);
void irq_rtc_seconds(void *data);
void irq_timer_led();

/**
 * Write content to the display.
 *
 * With the waveform replay running, content goes into the buffer and the refresh
 * picks it up; shifting and latching here as well would fight the replay and show
 * as a flick once a second. Without it, this is the shipping path unchanged.
 */
static inline void display_write(const char *buf, uint8_t dp)
{
  // With the engine running -- or the CPU replay standing in for it -- content
  // goes into the waveform and the refresh picks it up. Shifting and latching
  // here as well would fight it. Without either, this is the shipping path.
  if (display.engineRunning())
  {
    display.setContent(buf, dp);
    return;
  }

#ifdef SHIFT_ENGINE_VERIFY
  if (wave_verify)
  {
    display.setContent(buf, dp);
    return;
  }
#endif

  display.writeDisplay(buf, dp);
  display.latch();
}

/**
 * The menu-exit fade.
 *
 * Leaving the menu, the six positions come up one at a time from the left
 * rather than the time snapping back all at once. Position p starts at
 * p x FADE_STAGGER_MS and ramps to full over FADE_RAMP_MS.
 *
 *   pos 0  ####----
 *   pos 1    ####----
 *   pos 2      ####----      STAGGER 110 ms, RAMP 260 ms, total 810 ms
 *   pos 3        ####----
 *   pos 4          ####----
 *   pos 5            ####----
 *
 * Those timings were chosen by eye from five candidates spanning 320 to 810 ms,
 * and the choice says something about the quantisation worry. The design expected
 * eight linear levels to be coarse -- the step from unlit to one slice is about
 * 39% of the perceptual range -- and expected speed to be what hid it, making a
 * slower fade the riskier one. The slowest candidate was preferred. So the
 * stepping is not the binding constraint at this size and brightness, and none of
 * the ranked remedies (ramping global ~OE underneath, then N = 16, then temporal
 * dither) has turned out to be needed.
 *
 * Driven from elapsed time rather than by counting steps, so a slow pass through
 * the loop skips levels and still finishes on schedule instead of stretching.
 *
 * It changes brightness only. Content is never recomputed or re-shifted for it:
 * per-digit levels are independent of the character buffer, which is what lets
 * this sit beside update_leds() rather than inside render(), leaving the
 * time_dirty path exactly as cheap as it was.
 */
#define FADE_STAGGER_MS 110UL
#define FADE_RAMP_MS 260UL

#ifdef SHIFT_ENGINE_VERIFY
/* Tunable at runtime while the timings are being chosen by eye. Reflashing per
   combination makes comparing two of them useless: by the time the second is
   running, the first is a memory. Production uses the constants. */
static uint32_t fade_stagger_ms = FADE_STAGGER_MS;
static uint32_t fade_ramp_ms = FADE_RAMP_MS;
#define FADE_STAGGER fade_stagger_ms
#define FADE_RAMP fade_ramp_ms
#else
#define FADE_STAGGER FADE_STAGGER_MS
#define FADE_RAMP FADE_RAMP_MS
#endif

#define FADE_TOTAL_MS (5UL * FADE_STAGGER + FADE_RAMP)

static uint32_t fade_start_ms = 0;
static bool fade_active = false;

static void fade_begin()
{
  // Per-digit brightness only exists while the engine refreshes. Without it the
  // levels are inert, so there is nothing to fade.
  if (!display.engineRunning())
  {
    return;
  }

  fade_start_ms = millis();
  fade_active = true;
}

/**
 * Abandon the fade and put every position back to full.
 *
 * Never reverses or pauses: anything interrupting the fade wants the display
 * readable now. Leaving a digit part-way would show as a fault rather than a
 * flourish, and entering the menu with half-dim digits is the specific outcome
 * this prevents.
 */
static void fade_abort()
{
  if (!fade_active)
  {
    return;
  }

  fade_active = false;
  display.setAllDigitLevels(ShiftDisplay::maxDigitLevel());
}

static void update_display_fade()
{
  if (!fade_active)
  {
    return;
  }

  const uint8_t full = ShiftDisplay::maxDigitLevel();
  uint32_t elapsed = millis() - fade_start_ms;

  if (elapsed >= FADE_TOTAL_MS)
  {
    fade_active = false;
    display.setAllDigitLevels(full);
    return;
  }

  uint8_t levels[6];

  for (uint8_t p = 0; p < 6; p++)
  {
    uint32_t begins = (uint32_t)p * FADE_STAGGER;

    if (elapsed <= begins)
    {
      levels[p] = 0;
    }
    else
    {
      uint32_t into = elapsed - begins;
      levels[p] = (into >= FADE_RAMP)
                      ? full
                      : (uint8_t)((into * full) / FADE_RAMP_MS);
    }
  }

  display.setDigitLevels(levels);
}

static inline bool blink_on()
{
  return ((millis() / BLINK_PERIOD_MS) % 2) == 0;
}

void setup()
{
  // analogWrite's resolution and frequency are global, shared by the indicator
  // LEDs on TIM4 and the display's ~OE on TIM2. Set once, here, before anything
  // uses them -- tuning one feature must not silently change the other.
  analogWriteResolution(PWM_BITS);
  analogWriteFrequency(PWM_FREQ_HZ);

  setup_rtc();
  setup_user_btns();
  setup_user_leds();

  settings_load();

  display.begin(0U, PWM_MAX_DUTY);
  update_display_brightness();

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
  serial_commands_.AddCommand(&cmd_get_bright_);
  serial_commands_.AddCommand(&cmd_set_bright_);
  serial_commands_.AddCommand(&cmd_get_disp_);
  serial_commands_.AddCommand(&cmd_set_disp_);
  serial_commands_.AddCommand(&cmd_get_backup_);
#ifdef SHIFT_SWEEP
  serial_commands_.AddCommand(&cmd_set_nops_);
  serial_commands_.AddCommand(&cmd_set_pattern_);
#endif
#ifdef SHIFT_ENGINE_VERIFY
  serial_commands_.AddCommand(&cmd_wave_verify_);
  serial_commands_.AddCommand(&cmd_wave_level_);
  serial_commands_.AddCommand(&cmd_wave_dump_);
  serial_commands_.AddCommand(&cmd_wave_rate_);
  serial_commands_.AddCommand(&cmd_wave_engine_);
  serial_commands_.AddCommand(&cmd_wave_block_);
  serial_commands_.AddCommand(&cmd_wave_reset_);
  serial_commands_.AddCommand(&cmd_wave_tear_);
  serial_commands_.AddCommand(&cmd_wave_fade_);
#endif

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

  /* The fade triggers on a transition, not a state. Backing out, committing an
     editor and timing out all arrive at IDLE through this one update(), so all
     three fade with no special-casing. FIRING -> IDLE deliberately does not:
     dismissing an alarm is not a moment to wait half a second for the time. */
  State state_before = stateMachine.getState();

  stateMachine.update();

  State state_after = stateMachine.getState();

  bool fade_just_began = false;

  if (state_after == State::IDLE &&
      (state_before == State::MENU || state_before == State::EDIT))
  {
    fade_begin();
    fade_just_began = fade_active;
  }

  if (stateMachine.takeCommit())
  {
    commit_edit();
  }

  render();
  update_leds();
  update_display_brightness();

  /* Anything that wants the display readable now ends the fade: a button, a
     return to the menu, an alarm firing.
     
     Not on the pass that started it, though. Leaving the menu is *caused* by a
     button, and the button states are not cleared until the end of the loop, so
     the press that triggered the fade is still latched here and would abort it
     immediately -- every time, for every route out of the menu. That is why this
     worked when driven from the serial command and never once from the buttons. */
  if (fade_active && !fade_just_began &&
      (stateMachine.getState() != State::IDLE ||
       btnSetState != ButtonState::UNCHANGED ||
       btnPlusState != ButtonState::UNCHANGED ||
       btnMinusState != ButtonState::UNCHANGED))
  {
    fade_abort();
  }

  update_display_fade();

#ifdef SHIFT_ENGINE_VERIFY
  /* Replay every slice back to back, continuously, exactly as the DMA engine
     will. Running it here rather than inside render() matters: the engine
     refreshes whether or not the time changed, and the latch-the-previous-slice
     arrangement only reads correctly when slices follow each other without a
     gap. */
  if (wave_verify)
  {
    for (uint8_t s = 0; s < ShiftDisplay::maxDigitLevel(); s++)
    {
      display.shiftSliceByHand(s);
    }
  }
#endif

  // reset the button states
  btnSetState = ButtonState::UNCHANGED;
  btnPlusState = ButtonState::UNCHANGED;
  btnMinusState = ButtonState::UNCHANGED;
}

/**
 * The indicators are PWM-driven, not switched. PB6/PB7/PB8 are TIM4_CH1/CH2/CH3,
 * which analogWrite() resolves through PinMap_TIM without this code naming the
 * timer; the core's defaults are 8-bit duty at 1 kHz.
 *
 * Every write to these pins goes through set_led() and nowhere else. A stray
 * digitalWrite would reconfigure its pin back to plain output and silently stop
 * the timer driving it.
 */
// Last duty written to each indicator, in LED_TOP, LED_MID, LED_BOT order.
// Cached because analogWrite() reconfigures the timer channel on every call,
// which is far too heavy to repeat once per loop for three pins -- and read
// back by GL, since digitalRead() on a PWM'd pin samples the live waveform.
static uint16_t led_duty[3] = {0, 0, 0};

static void apply_leds(uint16_t top, uint16_t mid, uint16_t bot)
{
  if (top != led_duty[0])
  {
    led_duty[0] = top;
    analogWrite(LED_TOP, top);
  }
  if (mid != led_duty[1])
  {
    led_duty[1] = mid;
    analogWrite(LED_MID, mid);
  }
  if (bot != led_duty[2])
  {
    led_duty[2] = bot;
    analogWrite(LED_BOT, bot);
  }
}

/**
 * A smooth fade up and down, scaled so the peak is the configured brightness
 * rather than full duty -- an alarm that overrode a deliberately dim setting
 * would do so at exactly the hour that choice was made for. The trough stays
 * above zero so a glance mid-breath never reads as "not armed".
 */
static uint16_t breath_duty(uint16_t peak)
{
  // The triangle runs over 0..1023 rather than the full 12-bit range: the
  // smoothstep below squares it, and 4095 would overflow 32 bits.
  const uint32_t SPAN = 1023U;
  uint32_t half = BREATH_PERIOD_MS / 2;
  uint32_t phase = millis() % BREATH_PERIOD_MS;
  uint32_t tri = (phase < half) ? (phase * SPAN / half)
                                : ((BREATH_PERIOD_MS - phase) * SPAN / half);
  if (tri > SPAN)
  {
    tri = SPAN;
  }

  // smoothstep, t^2 * (3 - 2t), so the fade eases in and out rather than
  // reversing sharply at the extremes.
  uint32_t smooth = tri * tri * (3U * SPAN - 2U * tri) / (SPAN * SPAN);

  // Then gamma-shape it. The levels are already gamma-mapped, but the breath
  // interpolates between them and needs the same correction for the same
  // reason: equal steps of duty are not equal steps of apparent brightness, so
  // a linear fade appears to race through the dim end and crawl at the top.
  uint32_t shaped = smooth * smooth / SPAN;

  uint16_t trough = peak / 8;
  if (trough == 0)
  {
    trough = 1;
  }
  if (peak <= trough)
  {
    return peak;
  }

  return trough + (uint16_t)(((uint32_t)(peak - trough) * shaped) / SPAN);
}

// The display level in force right now, previewing an edit in progress for the
// same reason the indicators do -- and here the thing being adjusted is the very
// thing you are looking at.
static uint8_t effective_display_level()
{
  if (stateMachine.getState() == State::EDIT && edit_item == MENU_DISP)
  {
    return (edit_field[0] < DISP_LEVELS) ? edit_field[0] : (DISP_LEVELS - 1);
  }
  return (settings.displayLevel < DISP_LEVELS) ? settings.displayLevel
                                               : (DISP_LEVELS - 1);
}

void update_display_brightness()
{
  uint16_t duty = disp_gamma[effective_display_level()];
  if (duty != display.getBrightness())
  {
    display.setBrightness(duty);
  }
}

// The level in force right now: an editor in progress previews its value so the
// choice is made by eye, and backing out restores the committed setting because
// this falls straight back to it.
static uint8_t effective_indicator_level()
{
  if (stateMachine.getState() == State::EDIT && edit_item == MENU_BRIGHT)
  {
    return (edit_field[0] < LED_LEVELS) ? edit_field[0] : (LED_LEVELS - 1);
  }
  return (settings.indicatorLevel < LED_LEVELS) ? settings.indicatorLevel
                                                : (LED_LEVELS - 1);
}

void setup_user_leds()
{
  analogWrite(LED_TOP, 0);
  analogWrite(LED_MID, 0);
  analogWrite(LED_BOT, 0);
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

  boot_magic_seen = getBackupRegister(BKP_MAGIC_REG);
  boot_flags_seen = getBackupRegister(BKP_FLAGS_REG);
  boot_alarm_seen = getBackupRegister(BKP_ALARM_REG);
  boot_bright_seen = getBackupRegister(BKP_BRIGHT_REG);

  if (boot_magic_seen != BKP_MAGIC_VALUE)
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

  uint32_t brightness = getBackupRegister(BKP_BRIGHT_REG);
  settings.indicatorLevel = brightness & 0xFF;
  uint8_t storedDisplay = (brightness >> 8) & 0xFF;
  settings.displayLevel = storedDisplay ? (storedDisplay - 1) : (DISP_LEVELS / 2);
  if (settings.indicatorLevel >= LED_LEVELS)
  {
    settings.indicatorLevel = LED_LEVELS / 2;
  }
  if (settings.displayLevel >= DISP_LEVELS)
  {
    settings.displayLevel = DISP_LEVELS / 2;
  }

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

  setBackupRegister(BKP_BRIGHT_REG,
                    (((uint32_t)settings.displayLevel + 1) << 8) | settings.indicatorLevel);

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
  uint16_t peak = led_gamma[effective_indicator_level()];

  bool am = settings.mode12 && (date_time_buf.hours < 12);
  uint16_t bot;

  if (stateMachine.getState() == State::FIRING)
  {
    bot = breath_duty(peak);
  }
  else
  {
    bot = settings.alarmArmed ? peak : 0;
  }

  apply_leds(am ? peak : 0, settings.mode12 ? peak : 0, bot);
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

/**
 * Writes the hour, converted for the display mode, with the leading zero
 * suppressed in 12-hour mode -- ` 9 30 00` rather than `09 30 00`. 24-hour mode
 * keeps its leading zero, where it is correct.
 *
 * Both places that render the time go through this. Inlining the conditional
 * twice invites the alarm flash drifting out of step with the clock it flashes.
 */
static void put_hours(char *buf, uint8_t hours24, bool visible)
{
  uint8_t shown = display_hours(hours24);
  put2(buf, 0, shown, visible);

  if (visible && settings.mode12 && shown < 10)
  {
    buf[0] = ' ';
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
  case MENU_BRIGHT:
    label = " LEd  ";
    break;
  case MENU_DISP:
    label = " dISP ";
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

  case MENU_BRIGHT:
    // Levels read 1..8 rather than 0..7; the indicators themselves are the preview.
    memcpy(buf, "LEd   ", 6);
    put2(buf, 4, edit_field[0] + 1, show);
    break;

  case MENU_DISP:
    memcpy(buf, "dISP  ", 6);
    put2(buf, 4, edit_field[0] + 1, show);
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

#ifdef SHIFT_SWEEP
  if (test_pattern)
  {
    memcpy(buf, (test_pattern == 1) ? "888888" : "012345", 6);
    if (dp != last_rendered_dp || memcmp(buf, last_rendered, 6) != 0)
    {
      display_write(buf, dp);
      memcpy(last_rendered, buf, 6);
      last_rendered[6] = '\0';
      last_rendered_dp = dp;
    }
    else
    {
      // Keep re-shifting so a marginal rate has chances to fail, rather than
      // latching once and sitting on a result that happened to be correct.
      display_write(buf, dp);
    }
    return;
  }
#endif

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
      put_hours(buf, date_time_buf.hours, true);
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
    put_hours(buf, date_time_buf.hours, true);
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
    display_write(buf, dp);
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

  case MENU_BRIGHT:
    edit_field[0] = settings.indicatorLevel;
    break;

  case MENU_DISP:
    edit_field[0] = settings.displayLevel;
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

  case MENU_BRIGHT:
    *hi = LED_LEVELS - 1;
    break;

  case MENU_DISP:
    *hi = DISP_LEVELS - 1;
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

  case MENU_BRIGHT:
    settings.indicatorLevel = edit_field[0];
    settings_save();
    break;

  case MENU_DISP:
    settings.displayLevel = edit_field[0];
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


/**
 * Print::print() has no zero-padding and no hex width, which is the only reason
 * these call sites reached for printf -- and Print::printf drags in vdprintf ->
 * vasnprintf -> svfprintf -> printf_i, about 2.7 KB, to format "%d" and "%s".
 * These three do what the eleven call sites actually needed.
 */
static void print2(Print *out, uint8_t value)
{
  if (value < 10)
  {
    out->print('0');
  }
  out->print(value);
}

static void print4hex(Print *out, uint32_t value)
{
  static const char digits[] = "0123456789ABCDEF";
  out->print(digits[(value >> 12) & 0xF]);
  out->print(digits[(value >> 8) & 0xF]);
  out->print(digits[(value >> 4) & 0xF]);
  out->print(digits[value & 0xF]);
}

// "ERROR <FIELD> OUT OF RANGE {<value>}: <raw>", with no trailing newline, which
// is what these replies have always looked like.
static void print_range_error(Print *out, const char *field, int value, const char *raw)
{
  out->print("ERROR ");
  out->print(field);
  out->print(" OUT OF RANGE {");
  out->print(value);
  out->print("}: ");
  out->print(raw);
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
    print_range_error(sender->GetSerial(), "HOUR", hour, hour_str);
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
    print_range_error(sender->GetSerial(), "MINUTE", m, m_str);
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
    print_range_error(sender->GetSerial(), "SECOND", second, second_str);
    return;
  }

  date_time_buf.seconds = second;
  rtc.setSeconds(second);
}

/**
 * Civil-date arithmetic, replacing localtime() and mktime().
 *
 * Those two cost about 5.2 KB between them, and almost none of it is date
 * maths: localtime reaches tzset, which reaches sscanf to parse a TZ string
 * this firmware never sets, which drags in the whole formatted-input engine.
 * The conversion itself is two well-known integer routines over the proleptic
 * Gregorian calendar -- no tables, no locale, no timezone database.
 *
 * Days are counted from 1970-01-01. Month is 1..12 here and everywhere else;
 * see cmd_set_time for why that used to be 0..11 on this path alone.
 */
static int32_t days_from_civil(int32_t y, uint8_t m, uint8_t d)
{
  y -= (m <= 2);
  int32_t era = (y >= 0 ? y : y - 399) / 400;
  uint32_t yoe = (uint32_t)(y - era * 400);
  uint32_t doy = (153U * (m + (m > 2 ? -3 : 9)) + 2U) / 5U + d - 1U;
  uint32_t doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

static void civil_from_days(int32_t z, int32_t *y, uint8_t *m, uint8_t *d)
{
  z += 719468;
  int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  uint32_t doe = (uint32_t)(z - era * 146097);
  uint32_t yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
  int32_t yr = (int32_t)yoe + era * 400;
  uint32_t doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
  uint32_t mp = (5U * doy + 2U) / 153U;
  *d = (uint8_t)(doy - (153U * mp + 2U) / 5U + 1U);
  *m = (uint8_t)(mp + (mp < 10U ? 3U : -9U));
  *y = yr + (*m <= 2);
}

typedef struct
{
  int32_t year;   // full year, e.g. 2026
  uint8_t month;  // 1..12
  uint8_t day;
  uint8_t hours;
  uint8_t minutes;
  uint8_t seconds;
} CivilTime_t;

static void civil_from_timestamp(int32_t ts, CivilTime_t *out)
{
  int32_t days = ts / 86400;
  int32_t rem = ts % 86400;
  if (rem < 0)
  {
    rem += 86400;
    days -= 1;
  }

  civil_from_days(days, &out->year, &out->month, &out->day);
  out->hours = (uint8_t)(rem / 3600);
  out->minutes = (uint8_t)((rem % 3600) / 60);
  out->seconds = (uint8_t)(rem % 60);
}

// Returned 64-bit to match what mktime produced here: time_t is 64 bits in this
// toolchain, and an out-of-range date can exceed 32 bits -- GT printed exactly
// such a value before this change, and still must.
static int64_t timestamp_from_civil(const CivilTime_t *t)
{
  return (int64_t)days_from_civil(t->year, t->month, t->day) * 86400LL +
         (int64_t)t->hours * 3600LL + (int64_t)t->minutes * 60LL + t->seconds;
}

void cmd_set_time(SerialCommands *sender)
{
  char *timestampStr = sender->Next();
  if (timestampStr == NULL)
  {
    sender->GetSerial()->println("ERROR NO_TIMESTAMP");
    return;
  }

  int32_t timestamp = (int32_t)atol(timestampStr);
  timestamp += timezoneOffset * 60 * 60;

  CivilTime_t t;
  civil_from_timestamp(timestamp, &t);

  // Month is stored 1..12, matching the RTC and the menu editor. It used to be
  // stored 0..11 on this path only; ST and GT still round-tripped because the
  // same offset was applied on both sides, so the disagreement only showed
  // between the serial and menu paths.
  date_time_buf.hours = t.hours;
  date_time_buf.minutes = t.minutes;
  date_time_buf.seconds = t.seconds;
  date_time_buf.day = t.day;
  date_time_buf.month = t.month;
  date_time_buf.year = (uint8_t)(t.year - 2000);

  rtc.setHours(t.hours);
  rtc.setMinutes(t.minutes);
  rtc.setSeconds(t.seconds);
  rtc.setDay(t.day);
  rtc.setMonth(t.month);
  rtc.setYear((uint8_t)(t.year - 2000));

  sender->GetSerial()->println("OK");
}

void cmd_get_time(SerialCommands *sender)
{

  CivilTime_t t;
  t.year = 2000 + date_time_buf.year;
  t.month = date_time_buf.month;
  t.day = date_time_buf.day;
  t.hours = date_time_buf.hours;
  t.minutes = date_time_buf.minutes;
  t.seconds = date_time_buf.seconds;

  int64_t timestamp = timestamp_from_civil(&t);

  // Apply the timezone offset to change the time to UTC
  timestamp -= (int64_t)timezoneOffset * 60 * 60;

  // Return the current time as a Unix timestamp
  sender->GetSerial()->println((long long)timestamp);
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
    print_range_error(sender->GetSerial(), "MODE", mode, mode_str);
    return;
  }

  settings.mode12 = (mode == 12);
  settings_save();
  sender->GetSerial()->println("OK");
}

void cmd_get_alarm(SerialCommands *sender)
{
  // "<hh> <mm> <armed>"
  Print *out = sender->GetSerial();
  print2(out, settings.alarmHour);
  out->print(' ');
  print2(out, settings.alarmMinute);
  out->print(' ');
  out->println(settings.alarmArmed ? 1 : 0);
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
    print_range_error(sender->GetSerial(), "HOUR", hour, hour_str);
    return;
  }

  int minute = atoi(minute_str);
  if (minute < 0 || minute >= 60)
  {
    print_range_error(sender->GetSerial(), "MINUTE", minute, minute_str);
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
    print_range_error(sender->GetSerial(), "STATE", enable, enable_str);
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

void cmd_get_disp(SerialCommands *sender)
{
  // "<level> <duty>" -- the duty makes the active-low inversion checkable from a
  // host: a low level must read a low duty, not a high one.
  Print *out = sender->GetSerial();
  out->print(settings.displayLevel + 1);
  out->print(' ');
  out->println(disp_gamma[effective_display_level()]);
}

void cmd_set_disp(SerialCommands *sender)
{
  char *level_str = sender->Next();
  if (level_str == NULL)
  {
    sender->GetSerial()->println("ERROR NO_LEVEL");
    return;
  }

  int level = atoi(level_str);
  if (level < 1 || level > DISP_LEVELS)
  {
    print_range_error(sender->GetSerial(), "LEVEL", level, level_str);
    return;
  }

  settings.displayLevel = level - 1;
  settings_save();
  sender->GetSerial()->println("OK");
}

void cmd_get_backup(SerialCommands *sender)
{
  // "boot:<magic> <flags> <alarm> <bright>  now:<magic> <flags> <alarm> <bright>"
  // The boot values are what settings_load() saw; the now values are what the
  // registers hold at this moment.
  Print *out = sender->GetSerial();
  out->print("boot:");
  print4hex(out, boot_magic_seen);
  out->print(' ');
  print4hex(out, boot_flags_seen);
  out->print(' ');
  print4hex(out, boot_alarm_seen);
  out->print(' ');
  print4hex(out, boot_bright_seen);
  out->print(" now:");
  print4hex(out, getBackupRegister(BKP_MAGIC_REG));
  out->print(' ');
  print4hex(out, getBackupRegister(BKP_FLAGS_REG));
  out->print(' ');
  print4hex(out, getBackupRegister(BKP_ALARM_REG));
  out->print(' ');
  print4hex(out, getBackupRegister(BKP_BRIGHT_REG));
  out->print(" expect_magic:");
  print4hex(out, BKP_MAGIC_VALUE);
  out->println();
}

#ifdef SHIFT_ENGINE_VERIFY
void cmd_wave_verify(SerialCommands *sender)
{
  char *arg = sender->Next();
  Stream *out = sender->GetSerial();

  if (arg == NULL)
  {
    out->print("wave=");
    out->println(wave_verify ? 1 : 0);
    return;
  }

  wave_verify = (atoi(arg) != 0);

  if (wave_verify && display.engineRunning())
  {
    // Both drive the same pins. The engine wins unless it is told to stand down.
    display.stopEngine();
    out->println("engine stopped");
  }

  if (!wave_verify)
  {
    // Leaving replay: put the shipping path back in charge of what is latched.
    time_dirty = true;
  }

  out->print("wave=");
  out->println(wave_verify ? 1 : 0);
}

void cmd_wave_level(SerialCommands *sender)
{
  Stream *out = sender->GetSerial();
  char *pos_arg = sender->Next();

  if (pos_arg == NULL)
  {
    for (uint8_t i = 0; i < 6; i++)
    {
      out->print(display.getDigitLevel(i));
      out->print(i == 5 ? '\n' : ' ');
    }
    return;
  }

  char *lvl_arg = sender->Next();
  if (lvl_arg == NULL)
  {
    out->println("ERROR NO_LEVEL");
    return;
  }

  int pos = atoi(pos_arg);
  int lvl = atoi(lvl_arg);

  if (pos < 0 || pos > 5)
  {
    out->print("ERROR POS OUT OF RANGE: ");
    out->println(pos_arg);
    return;
  }

  if (lvl < 0 || lvl > (int)ShiftDisplay::maxDigitLevel())
  {
    out->print("ERROR LEVEL OUT OF RANGE: ");
    out->println(lvl_arg);
    return;
  }

  display.setDigitLevel((uint8_t)pos, (uint8_t)lvl);
  out->println("OK");
}

/**
 * Measure what the CPU replay actually achieves, flat out.
 *
 * The replay turned out not to flicker, which the proposal did not expect, so
 * the rate it reaches is worth a number rather than an inference. It also sets
 * the honest terms for what DMA buys: not "the CPU cannot do this" but "the CPU
 * need not spend itself doing it".
 *
 * Blocks the loop for the duration. It is a measurement in a test build.
 */
/**
 * Hold the main loop for a while, on purpose.
 *
 * The claim the engine makes is that the display refreshes whether or not the
 * CPU is paying attention. A loop that cannot run for two seconds is the
 * bluntest possible test of it, and the one worth doing: if the refresh is
 * secretly leaning on the loop, this is where it shows.
 */
/* Reset on demand, so "what does the display do coming up" is a repeatable
   observation rather than something glimpsed during a reflash. */
/**
 * Rebuild the waveform as fast as possible, repeatedly, and time it.
 *
 * Two things at once. The number says how long the DMA spends reading a buffer
 * that is being rewritten underneath it -- the tearing window. The burst is the
 * test: rebuilding thousands of times a second is far past anything the clock
 * will ever do, so if a mid-frame rewrite can produce something worse than a
 * momentary tear, this is where it appears.
 *
 * Each word is a single aligned 32-bit store, so no half-written word can ever
 * be read. The worst available outcome is some digits from the old content and
 * some from the new, for one frame.
 */
/* Run the fade without walking the menu, so it can be watched repeatedly while
   the timings are tuned by eye. Production carries neither this nor the flag. */
void cmd_wave_fade(SerialCommands *sender)
{
  Stream *out = sender->GetSerial();

  if (!display.engineRunning())
  {
    out->println("ERROR ENGINE_STOPPED");
    return;
  }

  char *a = sender->Next();
  if (a != NULL)
  {
    fade_stagger_ms = (uint32_t)atol(a);
    char *b = sender->Next();
    if (b != NULL)
    {
      fade_ramp_ms = (uint32_t)atol(b);
    }
  }

  fade_begin();

  out->print("fade stagger=");
  out->print(FADE_STAGGER);
  out->print(" ramp=");
  out->print(FADE_RAMP);
  out->print(" total=");
  out->println(FADE_TOTAL_MS);
}

void cmd_wave_tear(SerialCommands *sender)
{
  Stream *out = sender->GetSerial();
  char *arg = sender->Next();
  uint32_t n = (arg == NULL) ? 2000UL : (uint32_t)atol(arg);

  if (n > 20000UL)
  {
    n = 20000UL;
  }

  uint8_t keep[6];
  for (uint8_t i = 0; i < 6; i++)
  {
    keep[i] = display.getDigitLevel(i);
  }

  uint32_t t0 = micros();
  for (uint32_t i = 0; i < n; i++)
  {
    // Alternate the levels so the content genuinely changes every rebuild.
    display.setDigitLevel(0, (i & 1) ? keep[0] : (uint8_t)(keep[0] / 2 + 1));
  }
  uint32_t dt = micros() - t0;

  for (uint8_t i = 0; i < 6; i++)
  {
    display.setDigitLevel(i, keep[i]);
  }

  out->print("rebuilds=");
  out->print(n);
  out->print(" us_total=");
  out->print(dt);
  out->print(" ns_each=");
  out->print((uint32_t)((uint64_t)dt * 1000ULL / n));
  out->print(" frame_us=");
  out->println(1000000UL / SHIFT_ENGINE_FRAME_HZ);
}

void cmd_wave_reset(SerialCommands *sender)
{
  sender->GetSerial()->println("resetting");
  sender->GetSerial()->flush();
  delay(50);
  NVIC_SystemReset();
}

void cmd_wave_block(SerialCommands *sender)
{
  Stream *out = sender->GetSerial();
  char *arg = sender->Next();
  uint32_t ms = (arg == NULL) ? 2000UL : (uint32_t)atol(arg);

  if (ms > 10000UL)
  {
    ms = 10000UL;
  }

  out->print("blocking ");
  out->print(ms);
  out->println(" ms");
  out->flush();

  uint32_t t0 = millis();
  while ((millis() - t0) < ms)
  {
    __asm__ volatile("nop");
  }

  out->println("unblocked");
}

void cmd_wave_engine(SerialCommands *sender)
{
  Stream *out = sender->GetSerial();
  char *arg = sender->Next();

  if (arg == NULL)
  {
    out->print("engine=");
    out->println(display.engineRunning() ? 1 : 0);
    return;
  }

  if (atoi(arg) != 0)
  {
    wave_verify = false;
    display.startEngine();
  }
  else
  {
    display.stopEngine();
    time_dirty = true; // let the bit-banged path repaint
  }

  out->print("engine=");
  out->println(display.engineRunning() ? 1 : 0);
}

void cmd_wave_rate(SerialCommands *sender)
{
  Stream *out = sender->GetSerial();

  if (display.engineRunning())
  {
    /* Count CNDTR reloads over a fixed window. The counter walks 384 words down
       to 1 and reloads, once per frame, so a reload seen is a frame completed --
       measured from the hardware rather than inferred from the constants. */
    const uint32_t WINDOW_MS = 400;
    uint32_t t0 = millis();
    uint16_t last = (uint16_t)DMA1_Channel5->CNDTR;
    uint32_t wraps = 0;

    while ((millis() - t0) < WINDOW_MS)
    {
      uint16_t now = (uint16_t)DMA1_Channel5->CNDTR;
      if (now > last)
      {
        wraps++;
      }
      last = now;
    }

    out->print("dma frames=");
    out->print(wraps);
    out->print(" in_ms=");
    out->print(WINDOW_MS);
    out->print(" frame_hz=");
    out->print(wraps * 1000UL / WINDOW_MS);
    out->print(" target_frame_hz=");
    out->println(SHIFT_ENGINE_FRAME_HZ);
    return;
  }

  const uint16_t FRAMES = 200;

  uint32_t t0 = micros();
  for (uint16_t f = 0; f < FRAMES; f++)
  {
    for (uint8_t s = 0; s < ShiftDisplay::maxDigitLevel(); s++)
    {
      display.shiftSliceByHand(s);
    }
  }
  uint32_t dt = micros() - t0;

  if (dt == 0)
  {
    out->println("ERROR NO_ELAPSED");
    return;
  }

  uint32_t frame_hz = (uint32_t)((uint64_t)FRAMES * 1000000ULL / dt);
  uint32_t bits = (uint32_t)FRAMES * ShiftDisplay::maxDigitLevel() * SHIFT_ENGINE_BITS_PER_SLICE;
  uint32_t bit_hz = (uint32_t)((uint64_t)bits * 1000000ULL / dt);

  out->print("frames=");
  out->print(FRAMES);
  out->print(" us=");
  out->print(dt);
  out->print(" frame_hz=");
  out->print(frame_hz);
  out->print(" bit_hz=");
  out->print(bit_hz);
  out->print(" target_frame_hz=");
  out->print(SHIFT_ENGINE_FRAME_HZ);
  out->print(" target_bit_hz=");
  out->println(SHIFT_ENGINE_BIT_CLOCK_HZ);
}

void cmd_wave_dump(SerialCommands *sender)
{
  Stream *out = sender->GetSerial();

  out->print("slices=");
  out->print(SHIFT_ENGINE_SLICES);
  out->print(" words/slice=");
  out->print(SHIFT_ENGINE_WORDS_PER_SLICE);
  out->print(" bytes=");
  out->print(SHIFT_ENGINE_BUFFER_BYTES);
  out->print(" arr=");
  out->print(SHIFT_ENGINE_ARR);
  out->print(" ccr=");
  out->print(SHIFT_ENGINE_CCR_CLOCK);
  out->print(" bitclk=");
  out->print(SHIFT_ENGINE_BIT_CLOCK_HZ);
  out->print(" frame=");
  out->print(SHIFT_ENGINE_FRAME_HZ);
  out->print(" capable=");
  out->print(display.engineCapable() ? 1 : 0);
  out->print(" running=");
  out->print(display.engineRunning() ? 1 : 0);
  out->print(" cndtr=");
  out->print(DMA1_Channel5->CNDTR);
  out->print(" clkok=");
  out->print(shift_engine_clock_ok() ? 1 : 0);
  out->print(" coreclk=");
  out->print(SystemCoreClock);
  out->print(" assumed=");
  out->println((uint32_t)SHIFT_ENGINE_CORE_CLOCK_HZ);

  // The first four words of slice 0: enough to see the latch pulse riding in
  // words 0 and 1, and that no word touches the ~OE bit.
  const uint32_t *w = display.waveform();
  for (uint8_t i = 0; i < 4; i++)
  {
    out->print("w");
    out->print(i);
    out->print("=");
    print4hex(out, (uint16_t)(w[i] >> 16));
    print4hex(out, (uint16_t)(w[i] & 0xFFFF));
    out->print(i == 3 ? '\n' : ' ');
  }
}
#endif

#ifdef SHIFT_SWEEP
void cmd_set_nops(SerialCommands *sender)
{
  char *arg = sender->Next();
  if (arg == NULL)
  {
    sender->GetSerial()->print("nops=");
    sender->GetSerial()->println(shift_edge_nops);
    return;
  }
  shift_edge_nops = (uint8_t)atoi(arg);
  sender->GetSerial()->print("nops=");
  sender->GetSerial()->println(shift_edge_nops);
}

void cmd_set_pattern(SerialCommands *sender)
{
  char *arg = sender->Next();
  if (arg == NULL)
  {
    sender->GetSerial()->print("pattern=");
    sender->GetSerial()->println(test_pattern);
    return;
  }
  test_pattern = (uint8_t)atoi(arg);
  last_rendered[0] = '\0'; // force a redraw
  sender->GetSerial()->print("pattern=");
  sender->GetSerial()->println(test_pattern);
}
#endif

void cmd_get_leds(SerialCommands *sender)
{
  // "<top> <mid> <bot>" as duty values, 0-255. Lit means non-zero. Reporting
  // duty rather than a logical level keeps a breathing alarm observable from a
  // host, and digitalRead() is no longer meaningful here anyway: on a pin the
  // timer is driving it samples the live PWM waveform at an arbitrary phase.
  Print *out = sender->GetSerial();
  out->print(led_duty[0]);
  out->print(' ');
  out->print(led_duty[1]);
  out->print(' ');
  out->println(led_duty[2]);
}

void cmd_get_bright(SerialCommands *sender)
{
  sender->GetSerial()->println(settings.indicatorLevel + 1);
}

void cmd_set_bright(SerialCommands *sender)
{
  char *level_str = sender->Next();
  if (level_str == NULL)
  {
    sender->GetSerial()->println("ERROR NO_LEVEL");
    return;
  }

  int level = atoi(level_str);
  if (level < 1 || level > LED_LEVELS)
  {
    print_range_error(sender->GetSerial(), "LEVEL", level, level_str);
    return;
  }

  settings.indicatorLevel = level - 1;
  settings_save();
  sender->GetSerial()->println("OK");
}
