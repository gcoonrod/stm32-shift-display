// firmware (c) by Greg Coonrod
//
// firmware is licensed under a
// Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
//
// You should have received a copy of the license along with this
// work. If not, see <https://creativecommons.org/licenses/by-nc-sa/4.0/>.

#ifndef _SHIFTDISPLAYENGINE_H
#define _SHIFTDISPLAYENGINE_H

#include <Arduino.h>
#include "header.h"

/**
 * Timing constants for the DMA display engine.
 *
 * Every number here is derived rather than written down, because the one
 * relationship that matters is not obvious from any single value: a refresh
 * time slice must span a whole number of ~OE PWM periods.
 *
 * Why that matters. Per-digit brightness comes from lighting a digit in only
 * some of the slices, and global brightness comes from PWM on ~OE. The two
 * multiply. If their periods are unrelated the phase between them drifts and
 * near-coincident harmonics beat -- 4 kHz against a 1001 Hz frame is a visible
 * 4 Hz flutter. If they are locked but a slice covers only part of an ~OE
 * period, every digit's slices land at a fixed ~OE phase and take a
 * systematically wrong share of the on-time, which reads as a hardware fault
 * rather than a bug. A slice holding a whole number of ~OE periods removes the
 * phase term: each slice carries the same on-time whatever the phase, so a
 * digit lit in k of N slices is exactly k/N as bright.
 *
 * With the current constants it works out to exactly one period per slice:
 *
 *   ~OE 4 kHz from 72 MHz        -> 18000 counts per period
 *   48 bits per slice            ->   375 counts per bit
 *   two compare events per bit   -> 192 kHz bit clock, 500 Hz frame at N = 8
 *
 * Changing PWM_FREQ_HZ moves all of it. The static_asserts below fail the build
 * if the division stops being exact, rather than letting a plausible-looking
 * frequency skew per-digit brightness in a way nobody would connect to the edit.
 */

// Number of time slices per refresh frame. A digit's relative brightness is how
// many of these it is lit in, so this is also the number of per-digit levels.
//
// 8 is a deliberate stopping point, not a maximum. Perceived brightness goes as
// luminance^(1/2.2), so halving a perceptual step costs 4.6x the slices, while
// the frame rate is PWM_FREQ_HZ / N and falls as fast. 16 is the practical
// ceiling (250 Hz, 3072 bytes) and still leaves a 28% first step. Raising this
// is the *last* lever to reach for -- ramping global ~OE underneath buys more,
// for no RAM at all. See the change's design.md.
#ifndef SHIFT_ENGINE_SLICES
#define SHIFT_ENGINE_SLICES 8
#endif

// Six characters, eight bits each, one continuous 48-bit chain.
#define SHIFT_ENGINE_BITS_PER_SLICE 48

/**
 * The timer feeding the engine runs from the core clock. TIM1 is on APB2; with
 * the APB2 prescaler at 1 its counter clock is the core clock.
 *
 * This is written down rather than taken from F_CPU, and that is not laziness.
 * On this core F_CPU expands to SystemCoreClock -- a runtime variable the
 * clock-tree setup fills in -- so nothing derived from it can be a constant
 * expression, and none of the relationships below could be checked at build
 * time. The number is therefore a declared assumption, and shift_engine_clock_ok()
 * checks the running system against it before the engine is allowed to start.
 * A board brought up at a different clock fails loudly instead of shifting at
 * the wrong rate.
 */
#ifndef SHIFT_ENGINE_CORE_CLOCK_HZ
#define SHIFT_ENGINE_CORE_CLOCK_HZ 72000000UL
#endif
#define SHIFT_ENGINE_TIMER_HZ ((uint32_t)SHIFT_ENGINE_CORE_CLOCK_HZ)

// Counts in one ~OE PWM period -- the length a slice has to match.
#define SHIFT_ENGINE_COUNTS_PER_OE (SHIFT_ENGINE_TIMER_HZ / (uint32_t)PWM_FREQ_HZ)

// One bit period. Two compare events fall inside it: CH1 presents the data bit
// with the clock low, CH2 raises the clock.
#define SHIFT_ENGINE_COUNTS_PER_BIT \
    (SHIFT_ENGINE_COUNTS_PER_OE / (uint32_t)SHIFT_ENGINE_BITS_PER_SLICE)

// Timer reload. ARR counts from 0, so the period is ARR + 1.
#define SHIFT_ENGINE_ARR (SHIFT_ENGINE_COUNTS_PER_BIT - 1U)

// Compare values. CH1 sits at the start of the bit period and CH2 at its middle,
// giving a roughly 50% duty shift clock.
#define SHIFT_ENGINE_CCR_DATA 0U
#define SHIFT_ENGINE_CCR_CLOCK (SHIFT_ENGINE_COUNTS_PER_BIT / 2U)

// Reporting values -- not used to configure anything, but the numbers someone
// asking "how fast does this actually shift" needs.
#define SHIFT_ENGINE_BIT_CLOCK_HZ (SHIFT_ENGINE_TIMER_HZ / SHIFT_ENGINE_COUNTS_PER_BIT)
#define SHIFT_ENGINE_FRAME_HZ ((uint32_t)PWM_FREQ_HZ / (uint32_t)SHIFT_ENGINE_SLICES)

// One 32-bit BSRR word per bit. The rising clock edge carries no data, so it is
// a single constant word replayed by a second channel with memory increment off
// rather than another word per bit -- which is what keeps this at 48 and not 96.
#define SHIFT_ENGINE_WORDS_PER_SLICE SHIFT_ENGINE_BITS_PER_SLICE
#define SHIFT_ENGINE_WORDS_TOTAL \
    (SHIFT_ENGINE_WORDS_PER_SLICE * SHIFT_ENGINE_SLICES)
#define SHIFT_ENGINE_BUFFER_BYTES (SHIFT_ENGINE_WORDS_TOTAL * 4U)

/* ---- The relationships, enforced ---------------------------------------- */

// An ~OE period must be a whole number of timer counts, or the slice cannot
// match it however the rest divides.
static_assert(SHIFT_ENGINE_TIMER_HZ % (uint32_t)PWM_FREQ_HZ == 0,
              "PWM_FREQ_HZ must divide the core clock exactly: a slice has to "
              "span a whole number of ~OE periods or per-digit brightness "
              "depends on phase");

// The slice divides into 48 equal bit periods. This is the assertion that
// actually carries the lock.
static_assert(SHIFT_ENGINE_COUNTS_PER_OE % (uint32_t)SHIFT_ENGINE_BITS_PER_SLICE == 0,
              "One ~OE period must divide into 48 whole bit periods; adjust "
              "PWM_FREQ_HZ so that F_CPU / PWM_FREQ_HZ is a multiple of 48");

// Two compare events have to fit inside a bit period and be distinguishable.
static_assert(SHIFT_ENGINE_COUNTS_PER_BIT >= 4U,
              "Bit period too short for two separate compare events; "
              "PWM_FREQ_HZ is too high for this engine");

// ARR is 16-bit on TIM1.
static_assert(SHIFT_ENGINE_ARR <= 0xFFFFU,
              "Timer reload exceeds 16 bits; PWM_FREQ_HZ is too low");

// Below roughly 200 Hz the refresh is visible as flicker, particularly at the
// edge of vision. Frame rate is PWM_FREQ_HZ / SHIFT_ENGINE_SLICES, so this
// catches both an over-large slice count and an under-low ~OE frequency.
static_assert(SHIFT_ENGINE_FRAME_HZ >= 200U,
              "Refresh frame rate below 200 Hz will flicker; reduce "
              "SHIFT_ENGINE_SLICES or raise PWM_FREQ_HZ");

// The bit clock has to stay inside what the 74HC595 chain tolerates at 3.3 V.
// bsrr-shift-out swept this on the assembled board and found no failure at the
// fastest rate the bit-banged loop could reach, about 7 MHz, so that is a floor
// on the true limit rather than the limit itself. 1 MHz keeps a wide margin
// under the lowest number anyone has evidence for.
static_assert(SHIFT_ENGINE_BIT_CLOCK_HZ <= 1000000U,
              "Bit clock above 1 MHz exceeds the margin established by "
              "bsrr-shift-out; re-measure before raising this ceiling");

// A slice count that is not a power of two still works, but the per-digit
// brightness arithmetic is cheaper and exact when it is one.
static_assert((SHIFT_ENGINE_SLICES & (SHIFT_ENGINE_SLICES - 1)) == 0,
              "SHIFT_ENGINE_SLICES must be a power of two");

/**
 * The one relationship that cannot be checked at build time, because the core
 * clock is only known once the clock tree is configured. Every constant above
 * is derived from SHIFT_ENGINE_CORE_CLOCK_HZ; if the running system disagrees,
 * the derived bit clock and the slice-to-~OE lock are both wrong, so the engine
 * must not start.
 */
static inline bool shift_engine_clock_ok()
{
    return SystemCoreClock == (uint32_t)SHIFT_ENGINE_CORE_CLOCK_HZ;
}

#endif // _SHIFTDISPLAYENGINE_H
