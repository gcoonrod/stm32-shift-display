/**
 * stm32-595-display (c) by Greg
 *
 * stm32-595-display is licensed under a
 * Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 *
 * You should have received a copy of the license along with this
 * work. If not, see <https://creativecommons.org/licenses/by-nc-sa/4.0/>.
 */

#include <Arduino.h>
#include "./ShiftDisplay.h"

#define DP_BP 7
#define DP_BM 0b01000000
#define GET_BIT(byte, bit) (((byte) >> (bit)) & 0x01)

/**
 * Bit Position:  7   6   5   5   3   2   1   0
 * 595 Outputs:   QA  QB  QC  QD  QE  QF  QG  QH
 * 7 Segment LED: A   B   C   D   E   F   G   DP
 */
static const uint8_t segment_data[] = {
    0b11111100, // '0'
    0b01100000, // '1'
    0b11011010, // '2'
    0b11110010, // '3'
    0b01100110, // '4'
    0b10110110, // '5'
    0b10111110, // '6'
    0b11100000, // '7'
    0b11111110, // '8'
    0b11110110, // '9'
    0b11101110, // 'A'
    0b00011110, // 'B' (b)
    0b10011101, // 'C'
    0b01111010, // 'D' (d)
    0b10011110, // 'E'
    0b10001110, // 'F'
    0b10111100, // 'G'
    0b01101110, // 'H'
    0b00000000, // ' ' (space)
    
};

ShiftDisplay::ShiftDisplay(uint16_t data, uint16_t sclk, uint16_t sclr, uint16_t rclk, uint16_t oe)
{
    _serial_data_pin = data;
    _serial_clk_pin = sclk;
    _serial_clr_pin = sclr;
    _latch_clk_pin = rclk;
    _output_en_pin = oe;
    _initialized = false;
    _delay_us = 0;
    _delay_ms = 0;
}

void ShiftDisplay::update_buffer(const char *new_content)
{
    if (new_content == nullptr)
    {
        return;
    }

    size_t len = strlen(new_content);
    if (len > _char_buffer_size)
    {
        len = _char_buffer_size;
    }

    for (size_t i = 0; i < len; i++)
    {
        _buffer[i] = new_content[i];
    }
}

void ShiftDisplay::update_character(uint8_t index, char ascii, bool dp)
{
    if (index >= _char_buffer_size)
    {
        return;
    }

    _buffer[index] = ascii;
    _dp_state = dp ? SET_BIT(_dp_state, index) : CLEAR_BIT(_dp_state, index);
}

void ShiftDisplay::update_display()
{
    // Shift out the buffer in reverse order
    for (int i = _char_buffer_size - 1; i >= 0; i--)
    {
        shiftOutAscii(_buffer[i], GET_BIT(_dp_state, i));
    }
}

void ShiftDisplay::begin(uint32_t delay_us)
{
    pinMode(_serial_data_pin, OUTPUT);
    pinMode(_serial_clk_pin, OUTPUT);
    pinMode(_serial_clr_pin, OUTPUT);
    pinMode(_latch_clk_pin, OUTPUT);
    pinMode(_output_en_pin, OUTPUT);

    digitalWrite(_serial_data_pin, LOW);
    digitalWrite(_serial_clk_pin, LOW);
    digitalWrite(_serial_clr_pin, HIGH);
    digitalWrite(_latch_clk_pin, LOW);
    digitalWrite(_output_en_pin, HIGH);

    _delay_us = delay_us;

    _initialized = true;
}

void ShiftDisplay::update()
{
    if (!_initialized)
    {
        return;
    }

    update_display();
}

uint8_t ShiftDisplay::map_ascii(char hex_char)
{
    // Error checking: ensure input is a valid hexadecimal character
    if (!((hex_char >= '0' && hex_char <= '9') || (hex_char >= 'A' && hex_char <= 'H') || (hex_char == ' ')))
    {
        return 0b10101000; // Indicate invalid character with 3 horizontal bars
    }

    if (hex_char == ' ')
    {
        return 0;
    }

    // Convert character to index (subtract ASCII offset)
    uint8_t index = (hex_char <= '9') ? (hex_char - '0') : (hex_char - 'A' + 10);

    // Return the corresponding seven-segment pattern
    return segment_data[index];
}

void ShiftDisplay::shiftOutByte(uint8_t byte, bool dp)
{
    if (dp)
    {
        SET_BIT(byte, DP_BM);
    }
    for (uint8_t i = 0; i < 8; i++)
    {
        // Set serial data bit
        digitalWrite(_serial_data_pin, !!(byte & (1 << i)));
        if (_delay_us > 0)
        {
            delayMicroseconds(_delay_us);
        }

        // Pulse the serial clock
        digitalWrite(_serial_clk_pin, HIGH);
        if (_delay_us > 0)
        {
            delayMicroseconds(_delay_us);
        }
        digitalWrite(_serial_clk_pin, LOW);
        if (_delay_us > 0)
        {
            delayMicroseconds(_delay_us);
        }
    }
}

void ShiftDisplay::shiftOutAscii(char ascii, bool dp)
{
    shiftOutByte(map_ascii(ascii), dp);
}

void ShiftDisplay::writeDisplay(const char *buffer, uint8_t dp)
{
    update_buffer(buffer);
    _dp_state = dp;
    update_display();
}

void ShiftDisplay::enable()
{
    digitalWrite(_output_en_pin, LOW);
}

void ShiftDisplay::disable()
{
    digitalWrite(_output_en_pin, HIGH);
}

void ShiftDisplay::clear()
{
    // Clear the shift register
    digitalWrite(_serial_clr_pin, LOW);
    if (_delay_us)
    {
        delayMicroseconds(_delay_us);
    }
    digitalWrite(_serial_clr_pin, HIGH);
    if (_delay_us)
    {
        delayMicroseconds(_delay_us);
    }

    // Latch the cleared register
    digitalWrite(_latch_clk_pin, LOW);
    if (_delay_us)
    {
        delayMicroseconds(_delay_us);
    }
    digitalWrite(_latch_clk_pin, HIGH);
    if (_delay_us)
    {
        delayMicroseconds(_delay_us);
    }
}

void ShiftDisplay::latch()
{
    // Latch the shift register
    digitalWrite(_latch_clk_pin, LOW);
    if (_delay_us)
    {
        delayMicroseconds(_delay_us);
    }
    digitalWrite(_latch_clk_pin, HIGH);
    if (_delay_us)
    {
        delayMicroseconds(_delay_us);
    }
}

inline size_t ShiftDisplay::write(uint8_t value)
{
    if ((char)value == '\r' || (char)value == '\n')
    {
        // clear();
        return 1;
    }

    shiftOutAscii(value);
    return 1; // Assume success
}

inline size_t ShiftDisplay::write(const uint8_t *buffer, size_t size)
{
    if (buffer == nullptr || size == 0)
    {
        return 0;
    }

    size_t n = 0;
    const uint8_t *end = buffer + size;
    while (end != buffer)
    {
        --end;
        if (write(*end))
        {
            n++;
        }
        else
        {
            break;
        }
    }
    return n;
}
