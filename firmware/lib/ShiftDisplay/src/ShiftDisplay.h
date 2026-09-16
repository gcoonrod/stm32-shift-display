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
    uint32_t _delay_us; // kept for API compatibility; see begin()

    /**
     * Resolved once in begin(). digitalWrite() spends ~35 cycles turning a pin
     * number back into a port and a bit mask, every time, to reach a single
     * store -- and the answer never changes, because the pins are fixed at
     * construction.
     *
     * Each pin gets a BSRR set word and a clear word. BSRR's low half sets and
     * its high half clears, so one store drives only the bits named in it:
     * ~OE shares this port and belongs to the brightness timer, and a port-wide
     * write would reach across it.
     */
    GPIO_TypeDef *_data_port;
    GPIO_TypeDef *_clk_port;
    GPIO_TypeDef *_clr_port;
    GPIO_TypeDef *_latch_port;

    uint32_t _data_set, _data_clr;
    uint32_t _clk_set, _clk_clr;
    uint32_t _srclr_set, _srclr_clr;
    uint32_t _latch_set, _latch_clr;

    // Data and clock share a port on this board, so one store can present a bit
    // and drop the clock together. Not assumed -- begin() checks, and falls back
    // to separate writes if they ever differ.
    bool _same_port;
    uint32_t _bit1_clklow, _bit0_clklow;

    /**
     * ~OE's port and bit, resolved alongside the others so that begin() can
     * prove no word this driver composes can reach it.
     *
     * The pin numbers are checked for distinctness at build time where they are
     * defined, but that only catches an alias. This catches the subtler case:
     * two different pin numbers landing on the same port bit, which the variant
     * pin map could in principle do and which no amount of reading the pin
     * defines would reveal. A word replayed by DMA shows nothing in the
     * instruction stream when it is wrong, so it is worth proving rather than
     * assuming.
     */
    GPIO_TypeDef *_oe_port;
    uint32_t _oe_mask;
    bool _pins_safe;

    // Private display state vars
    uint8_t _dp_state = 0b00000000; // [0:5] -> [0:5] DP state for each character, [6:7] -> Not used
    static const uint8_t _char_buffer_size = 6;
    char _buffer[_char_buffer_size] = {' ', ' ', ' ', ' ', ' ', ' '};

protected:
    // Writes the output-enable line. ~OE is active low, so the duty written to
    // the pin is the complement of the brightness: this is the one place that
    // inversion lives, so no caller has to remember it.
    void write_output_enable(uint16_t brightness);

    // True if any driven pin shares a port bit with ~OE. Checked once in begin();
    // the driver refuses to initialise if it is.
    bool oe_collides() const;

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