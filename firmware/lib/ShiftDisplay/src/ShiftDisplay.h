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
    uint32_t _delay_us;
    uint32_t _delay_ms;

    // Private display state vars
    uint8_t _dp_state = 0b00000000; // [0:5] -> [0:5] DP state for each character, [6:7] -> Not used
    static const uint8_t _char_buffer_size = 6;
    char _buffer[_char_buffer_size] = {' ', ' ', ' ', ' ', ' ', ' '};

protected:
    uint8_t map_ascii(char hex_char);
    void update_buffer(const char* new_content);
    void update_character(uint8_t index, char ascii, bool dp);
    void update_display();

public:
    ShiftDisplay(uint16_t data, uint16_t sclk, uint16_t sclr, uint16_t rclk, uint16_t oe);
    void begin(uint32_t delay_us);
    void begin()
    {
        begin(0U);
    }

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