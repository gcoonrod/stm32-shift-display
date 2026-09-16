// firmware (c) by Greg Coonrod
// 
// firmware is licensed under a
// Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
// 
// You should have received a copy of the license along with this
// work. If not, see <https://creativecommons.org/licenses/by-nc-sa/4.0/>.

#ifndef _SHIFT_CLOCK_HEADER_H
#define _SHIFT_CLOCK_HEADER_H

#include <inttypes.h>

/**
 * PWM configuration, shared rather than local because two things depend on it
 * that live in different translation units: the indicator LEDs and the display's
 * ~OE, both driven through analogWrite from main.cpp, and the display engine's
 * timer constants, derived from PWM_FREQ_HZ in ShiftDisplayEngine.h.
 *
 * It is one number in one place deliberately. The engine locks a refresh time
 * slice to exactly one ~OE period, and that relationship is what keeps the two
 * brightness mechanisms from beating against each other. A second copy of this
 * frequency that drifted from the first would break the lock silently.
 *
 * Indicators run at 12-bit PWM, not the core's 8-bit default. Eight bits is
 * plenty for steady indicators but not for fading between them: at the dim end
 * a single duty step is a large fraction of the light output -- duty 4 to 5 is a
 * 25% jump -- so a breath built on 256 steps visibly staircases however fast it
 * is updated. 4096 steps put those jumps below the threshold where the eye
 * separates them. MAX_PWM_RESOLUTION is 16, so this is well inside what the
 * core supports.
 */
#define PWM_BITS 12
#define PWM_MAX_DUTY 4095

/* 1 kHz would probably do, but a bright high-contrast source at low duty seen at
   the edge of vision is where PWM flicker gets noticed, and a clock is looked at
   sideways constantly. 4 kHz costs nothing: the timer reload is still about
   18000 counts, far more than the 4096 duty steps, so resolution is untouched.
   Resolution would only start to suffer above roughly 17 kHz.

   Changing this moves the display engine's timer constants with it. The engine
   static_asserts that the result still divides exactly; a frequency that does not
   fails the build rather than skewing per-digit brightness. */
#define PWM_FREQ_HZ 4000

typedef struct {
    uint8_t week_day;
    uint8_t day;
    uint8_t month;
    uint8_t year;
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
} DateTimeBuffer_t;

#endif // _SHIFT_CLOCK_HEADER_H
