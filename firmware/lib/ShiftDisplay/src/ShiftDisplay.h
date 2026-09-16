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
#include "ShiftDisplayEngine.h"

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

    /**
     * The refresh waveform: one BSRR word per bit, SHIFT_ENGINE_SLICES slices of
     * 48 bits each. A DMA channel replays this into the port with memory
     * increment on, one word per timer compare event.
     *
     * Each word presents a data bit with the shift clock held low. The rising
     * clock edge carries no data, so it is a single constant word replayed by a
     * second channel with increment off -- which is what keeps this at 48 words
     * per slice rather than 96.
     *
     * The latch rides in the first two words of every slice: word 0 raises RCLK
     * and word 1 drops it, so the edge at the start of slice k+1 presents what
     * slice k shifted in. The display therefore runs one slice behind, which is
     * 2 ms of constant offset on a stream that never ends and nothing can
     * observe. It costs no extra words and no third channel; appending latch
     * words instead would not work, because the clock channel keeps firing
     * during them and extra clocks push data off a chain exactly as long as its
     * data.
     */
    uint32_t _wave[SHIFT_ENGINE_WORDS_TOTAL];

    // The one word the clock channel replays. It lives in memory rather than
    // being written by the CPU because a DMA channel needs a source address.
    uint32_t _clk_high_word;

    // Per-position relative brightness, in slices out of SHIFT_ENGINE_SLICES.
    // Full for every position unless something dims one.
    uint8_t _digit_levels[_char_buffer_size];

    // The engine drives data, clock and latch from one port with one channel, so
    // all three must share a port. Checked in begin() rather than assumed.
    bool _engine_capable;

    // True while the timer and both DMA channels are running. The bit-banged
    // shift and latch paths must not run at the same time; they would fight the
    // replay for the same pins.
    bool _engine_running;

protected:
    // Writes the output-enable line. ~OE is active low, so the duty written to
    // the pin is the complement of the brightness: this is the one place that
    // inversion lives, so no caller has to remember it.
    void write_output_enable(uint16_t brightness);

    // True if any driven pin shares a port bit with ~OE. Checked once in begin();
    // the driver refuses to initialise if it is.
    bool oe_collides() const;

    // Rebuild the whole waveform from the character buffer, the decimal points
    // and the per-digit levels.
    void build_waveform();

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

    /**
     * Per-digit relative brightness, 0..SHIFT_ENGINE_SLICES.
     *
     * This is a second modulation multiplying the global ~OE brightness rather
     * than replacing it: a digit at full here is as bright as the display's
     * configured level allows, and no brighter.
     */
    void setDigitLevel(uint8_t index, uint8_t slices);
    uint8_t getDigitLevel(uint8_t index) const;
    void setAllDigitLevels(uint8_t slices);

    // Set every position at once. One rebuild rather than six, which matters to
    // the fade: it steps all six positions together, and six separate rebuilds
    // would cost a millisecond of CPU per step to no purpose.
    void setDigitLevels(const uint8_t *slices);
    static uint8_t maxDigitLevel() { return SHIFT_ENGINE_SLICES; }

    // False if the pin map puts data, clock and latch on different ports, in
    // which case one DMA channel cannot drive them and the engine cannot run.
    bool engineCapable() const { return _engine_capable; }

    /**
     * Hand the display to the timer and DMA, or take it back.
     *
     * startEngine() refuses unless the pins allow it and the running core clock
     * matches the one the timing constants were derived from. Refusing leaves
     * the display blank, which is a state someone investigates; shifting at the
     * wrong rate is one they misdiagnose.
     */
    void startEngine();
    void stopEngine();
    bool engineRunning() const { return _engine_running; }

    // Read-only view of the refresh waveform, for verification.
    const uint32_t *waveform() const { return _wave; }

    /**
     * Shift one slice out through the CPU, writing exactly the words and in
     * exactly the order the two DMA channels will.
     *
     * This exists so that "the words are right" can be established before any
     * peripheral is configured. Debugging a DMA display by eye is hard enough
     * without also wondering whether the buffer it is replaying was ever
     * correct.
     */
    void shiftSliceByHand(uint8_t slice);

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

    // Update the character buffer and rebuild the waveform, without shifting
    // anything out. Once the engine is running this is all a caller needs; the
    // refresh picks the new words up on its next pass.
    void setContent(const char* buffer, uint8_t dp);

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