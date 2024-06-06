// firmware (c) by Greg Coonrod
// 
// firmware is licensed under a
// Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
// 
// You should have received a copy of the license along with this
// work. If not, see <https://creativecommons.org/licenses/by-nc-sa/4.0/>.

#include <inttypes.h>

typedef struct {
    uint8_t week_day;
    uint8_t day;
    uint8_t month;
    uint8_t year;
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
} DateTimeBuffer_t;