// firmware (c) by Greg Coonrod
//
// firmware is licensed under a
// Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
//
// You should have received a copy of the license along with this
// work. If not, see <https://creativecommons.org/licenses/by-nc-sa/4.0/>.

#include "./ShiftClock.h"

ShiftClock::ShiftClock(uint8_t hours, uint8_t minutes, uint8_t seconds)
{
    // TODO: sanitize inputs
    _clock.hours = hours;
    _clock.minutes = minutes;
    _clock.seconds = seconds;
}

ShiftClock::ShiftClock(ShiftClock_t clock)
{
    _clock.hours = clock.hours;
    _clock.minutes = clock.minutes;
    _clock.seconds = clock.seconds;
}

ShiftClock::ShiftClock()
{
    _clock.hours = 0;
    _clock.minutes = 0;
    _clock.seconds = 0;
}

void ShiftClock::setHours(uint8_t hours)
{
    if (hours > 23)
        return;

    _clock.hours = hours;
}

void ShiftClock::setMinutes(uint8_t minutes)
{
    if (minutes > 59)
        return;
    _clock.minutes = minutes;
}

void ShiftClock::setSeconds(uint8_t seconds)
{
    if (seconds > 59)
        return;
    _clock.seconds = seconds;
}

uint8_t ShiftClock::getHours()
{
    return _clock.hours;
}

uint8_t ShiftClock::getMinutes()
{
    return _clock.minutes;
}

uint8_t ShiftClock::getSeconds()
{
    return _clock.seconds;
}

void ShiftClock::tick()
{
    _clock.seconds++;
    if (_clock.seconds >= 60) {
        _clock.seconds = 0;

        _clock.minutes++;
        if (_clock.minutes >= 60) {
            _clock.minutes = 0;

            _clock.hours++;
            if (_clock.hours >= 24) {
                _clock.hours = 0;
            }
        }
    }
}