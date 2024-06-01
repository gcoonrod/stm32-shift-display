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

#define DP_BP 2
#define DP_BM 0b00000010
#define SET_DP(reg) ((reg) |= (1U << 1))
#define UNSET_DP(reg) ((reg) &= ~(1U << 1))

/**
 * Bit Position:  7   6   5   5   3   2   1   0
 * 595 Outputs:   QA  QB  QC  QD  QE  QF  QG  QH
 * 7 Segment LED: D   E   G   F   A   B   DP  C
 */
static const uint8_t segment_data[] = {
    0b11011101, // '0'
    0b00000101, // '1'
    0b11101100, // '2'
    0b10101101, // '3'
    0b00110101, // '4'
    0b10111001, // '5'
    0b11111001, // '6'
    0b00001101, // '7'
    0b11111101, // '8'
    0b10111101, // '9'
    0b01111101, // 'A'
    0b11110001, // 'b'
    0b11011000, // 'C'
    0b11100101, // 'd'
    0b11111000, // 'E'
    0b01111000  // 'F'
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

void ShiftDisplay::begin()
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

    _initialized = true;
}

uint8_t ShiftDisplay::map_ascii(char hex_char)
{
    // Error checking: ensure input is a valid hexadecimal character
    if (!((hex_char >= '0' && hex_char <= '9') || (hex_char >= 'A' && hex_char <= 'F')))
    {
        if (hex_char == ' ')
        {
            return 0;
        }
        return 0b10101000; // Indicate invalid character with 3 horizontal bars
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
        SET_DP(byte);
    }
    for (uint8_t i = 0; i < 8; i++)
    {
        // Set serial data bit
        digitalWrite(_serial_data_pin, !!(byte & (1 << i)));
        if (_delay_us > 0)
        {
            delayMicroseconds(_delay_us);
        }

        // Pulse the serial and latch (srclk & rclk) together
        digitalWrite(_serial_clk_pin, HIGH);
        digitalWrite(_latch_clk_pin, HIGH);
        if (_delay_us > 0)
        {
            delayMicroseconds(_delay_us);
        }
        digitalWrite(_serial_clk_pin, LOW);
        digitalWrite(_latch_clk_pin, LOW);
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
