// firmware (c) by Greg Coonrod
//
// firmware is licensed under a
// Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
//
// You should have received a copy of the license along with this
// work. If not, see <https://creativecommons.org/licenses/by-nc-sa/4.0/>.

#ifndef __SHIFT_CLOCK_H
#define __SHIFT_CLOCK_H

#include <inttypes.h>

typedef struct
{
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
} ShiftClock_t;

class ShiftClock
{
private:
    ShiftClock_t _clock;

public:
    ShiftClock();
    ShiftClock(uint8_t hours, uint8_t minutes, uint8_t seconds);
    ShiftClock(ShiftClock_t clock);

    void setHours(uint8_t hours);
    void setMinutes(uint8_t minutes);
    void setSeconds(uint8_t seconds);

    uint8_t getHours();
    uint8_t getMinutes();
    uint8_t getSeconds();

    void tick();
};

#endif // __SHIFT_CLOCK_H