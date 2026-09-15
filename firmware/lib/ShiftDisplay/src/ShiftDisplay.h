// stm32-595-display (c) by Greg
//
// stm32-595-display is licensed under a
// Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
//
// You should have received a copy of the license along with this
// work. If not, see <https://creativecommons.org/licenses/by-nc-sa/4.0/>.

#ifndef __ShiftDisplay_H
#define __ShiftDisplay_H

#include <Arduino.h>
#include "Print.h"

class ShiftDisplay: public Print
{
private:
    // 74x595 control pins
    uint16_t _serial_data_pin;
    uint16_t _serial_clk_pin;
    uint16_t _serial_clr_pin; // low-level logic
    uint16_t _latch_clk_pin;
    uint16_t _output_en_pin; // low-level logic

    // Private state vars
    bool _initialized;
    uint16_t _brightness; // 0 = blank, _max_duty = full
    uint16_t _max_duty;
    uint32_t _delay_us;
    uint32_t _delay_ms;

    // Private display state vars
    uint8_t _dp_state = 0b00000000; // [0:5] -> [0:5] DP state for each character, [6:7] -> Not used
    static const uint8_t _char_buffer_size = 6;
    char _buffer[_char_buffer_size] = {' ', ' ', ' ', ' ', ' ', ' '};

protected:
    // Writes the output-enable line. ~OE is active low, so the duty written to
    // the pin is the complement of the brightness: this is the one place that
    // inversion lives, so no caller has to remember it.
    void write_output_enable(uint16_t brightness);

    uint8_t map_ascii(char ascii);
    void update_buffer(const char* new_content);
    void update_character(uint8_t index, char ascii, bool dp);
    void update_display();

public:
    ShiftDisplay(uint16_t data, uint16_t sclk, uint16_t sclr, uint16_t rclk, uint16_t oe);

    // max_duty must match the resolution given to analogWriteResolution(); that
    // setting is global to analogWrite and is shared with anything else using it.
    void begin(uint32_t delay_us, uint16_t max_duty);
    void begin(uint32_t delay_us)
    {
        begin(delay_us, 255U);
    }
    void begin()
    {
        begin(0U, 255U);
    }

    // Brightness is PWM on ~OE, which is shared by every shift register, so it
    // applies to all six digits at once.
    void setBrightness(uint16_t duty);
    uint16_t getBrightness() const { return _brightness; }
    uint16_t maxDuty() const { return _max_duty; }

    void update();

    void shiftOutByte(uint8_t byte, bool dp);
    void shiftOutByte(uint8_t byte)
    {
        shiftOutByte(byte, false);
    }

    void shiftOutAscii(char ascii, bool dp);
    void shiftOutAscii(char ascii)
    {
        shiftOutAscii(ascii, false);
    }

    void writeDisplay(const char* buffer, uint8_t dp);

    void enable();
    void disable();

    void clear();
    void latch();

    virtual size_t write(uint8_t);
    virtual size_t write(const uint8_t *buffer, size_t size);

    using Print::write;
};

#endif //__ShiftDisplay_H