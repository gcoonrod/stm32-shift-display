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
#include "./ShiftDisplayEngine.h"

/**
 * Inter-edge throttle for the shift clock.
 *
 * Six 74HC595s on 3.3 V: the family is characterised at 4.5 V, and for a chain
 * this deep the binding constraint is each stage's serial propagation delay plus
 * the next stage's setup time, six times over. An unthrottled BSRR loop clocks
 * at roughly 9-12 MHz, which may be past that edge -- and the far end of the
 * chain is not wired back, so a marginal rate shows up only as the occasional
 * wrong segment.
 *
 * The value is established by measurement on the assembled board, not by
 * calculation. It does not need to be precise, only monotonic: more NOPs means
 * a slower clock. Build with -D SHIFT_SWEEP to make it settable at runtime so
 * the failure point can be found without reflashing for every step.
 */
#ifndef SHIFT_EDGE_NOPS
#define SHIFT_EDGE_NOPS 0
#endif

#ifdef SHIFT_SWEEP
volatile uint8_t shift_edge_nops = SHIFT_EDGE_NOPS;
#define SHIFT_EDGE_DELAY()                              \
    do                                                  \
    {                                                   \
        for (uint8_t _n = 0; _n < shift_edge_nops; _n++) \
        {                                               \
            __asm__ volatile("nop");                    \
        }                                               \
    } while (0)
#elif SHIFT_EDGE_NOPS > 0
#define SHIFT_EDGE_DELAY()                                 \
    do                                                     \
    {                                                      \
        for (uint32_t _n = 0; _n < SHIFT_EDGE_NOPS; _n++)  \
        {                                                  \
            __asm__ volatile("nop");                       \
        }                                                  \
    } while (0)
#else
#define SHIFT_EDGE_DELAY() \
    do                     \
    {                      \
    } while (0)
#endif

#define DP_BP 1
#define DP_BM 0b00000001
#define GET_BIT(byte, bit) (((byte) >> (bit)) & 0x01)

/**
 * Bit Position:  7   6   5   4   3   2   1   0
 * 595 Outputs:   QA  QB  QC  QD  QE  QF  QG  QH
 * 7 Segment LED: A   B   C   D   E   F   G   DP
 */

/**
 * Segment bits, named. Glyphs below are written as unions of these rather than
 * as binary literals: SEG_E | SEG_F is either right or obviously wrong, whereas
 * 0b00011000 is neither, and the compiler accepts any eight bits you hand it.
 * Three glyph bugs were written as literals before this existed -- a missing
 * entry, a stray decimal point, and one built from the wrong two bits.
 *
 * The compiler folds these, so the table costs exactly what it did before.
 */
#define SEG_A 0b10000000  /* top          */
#define SEG_B 0b01000000  /* upper right  */
#define SEG_C 0b00100000  /* lower right  */
#define SEG_D 0b00010000  /* bottom       */
#define SEG_E 0b00001000  /* lower left   */
#define SEG_F 0b00000100  /* upper left   */
#define SEG_G 0b00000010  /* middle       */
#define SEG_DP 0b00000001 /* decimal point -- driven from _dp_state, never set in a glyph */

// Shown for any character with no glyph, so an unrenderable character fails
// visibly rather than as a blank.
#define SEG_INVALID (SEG_A | SEG_C | SEG_E)

/**
 * Glyph patterns indexed by ASCII code, offset by SEG_FIRST_CHAR. This table is
 * the single source of truth for what the display can render: a character is
 * renderable exactly when it has a non-zero entry here. The previous form kept a
 * hand-written range check in map_ascii() alongside a densely packed table, so
 * adding a glyph meant editing both in lockstep -- and a character present in one
 * but missing from the other rendered as SEG_INVALID, which reads as a hardware
 * fault rather than a typo.
 *
 * Space is deliberately handled before this lookup, since its pattern (all
 * segments off) is indistinguishable from "no glyph defined".
 *
 * M and W are absent on purpose: neither renders legibly on seven segments, so
 * labels are chosen from what the display can actually show.
 */
#define SEG_FIRST_CHAR ' '
#define SEG_LAST_CHAR 'u'

static const uint8_t segment_data[] = {
    /* ' ' */ 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* ! " # $ % & ' ( ) * + , */
    /* '-' */ SEG_G,
    0, 0,                                /* . / */
    /* '0' */ SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,
    /* '1' */ SEG_B | SEG_C,
    /* '2' */ SEG_A | SEG_B | SEG_D | SEG_E | SEG_G,
    /* '3' */ SEG_A | SEG_B | SEG_C | SEG_D | SEG_G,
    /* '4' */ SEG_B | SEG_C | SEG_F | SEG_G,
    /* '5' */ SEG_A | SEG_C | SEG_D | SEG_F | SEG_G,
    /* '6' */ SEG_A | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,
    /* '7' */ SEG_A | SEG_B | SEG_C,
    /* '8' */ SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,
    /* '9' */ SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G,
    0, 0, 0, 0, 0, 0, 0,                 /* : ; < = > ? @ */
    /* 'A' */ SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G,
    /* 'B' */ SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,   /* lowercase b */
    /* 'C' */ SEG_A | SEG_D | SEG_E | SEG_F,
    /* 'D' */ SEG_B | SEG_C | SEG_D | SEG_E | SEG_G,   /* lowercase d */
    /* 'E' */ SEG_A | SEG_D | SEG_E | SEG_F | SEG_G,
    /* 'F' */ SEG_A | SEG_E | SEG_F | SEG_G,
    /* 'G' */ SEG_A | SEG_C | SEG_D | SEG_E | SEG_F,
    /* 'H' */ SEG_B | SEG_C | SEG_E | SEG_F | SEG_G,
    /* 'I' */ SEG_E | SEG_F,   /* the two left verticals */
    0, 0,                                /* J K */
    /* 'L' */ SEG_D | SEG_E | SEG_F,
    0,                                   /* M */
    /* 'N' */ SEG_C | SEG_E | SEG_G,   /* lowercase n */
    /* 'O' */ SEG_C | SEG_D | SEG_E | SEG_G,   /* lowercase o */
    /* 'P' */ SEG_A | SEG_B | SEG_E | SEG_F | SEG_G,
    0,                                   /* Q */
    /* 'R' */ SEG_E | SEG_G,   /* lowercase r */
    /* 'S' */ SEG_A | SEG_C | SEG_D | SEG_F | SEG_G,   /* same shape as '5', as is conventional */
    /* 'T' */ SEG_D | SEG_E | SEG_F | SEG_G,   /* lowercase t */
    /* 'U' */ SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* V W X Y Z [ \ ] ^ _ ` a b c */
    /* 'd' */ SEG_B | SEG_C | SEG_D | SEG_E | SEG_G,
    0, 0, 0, 0,                          /* e f g h */
    /* 'i' */ SEG_E | SEG_F,
    0, 0,                                /* j k */
    /* 'l' */ SEG_D | SEG_E | SEG_F,
    0,                                   /* m */
    /* 'n' */ SEG_C | SEG_E | SEG_G,
    /* 'o' */ SEG_C | SEG_D | SEG_E | SEG_G,
    /* 'p' */ SEG_A | SEG_B | SEG_E | SEG_F | SEG_G,
    0,                                   /* q */
    /* 'r' */ SEG_E | SEG_G,
    /* 's' */ SEG_A | SEG_C | SEG_D | SEG_F | SEG_G,
    /* 't' */ SEG_D | SEG_E | SEG_F | SEG_G,
    /* 'u' */ SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,
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
    _brightness = 0;
    _max_duty = 255;
    _same_port = false;
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

/**
 * Rebuild the refresh waveform.
 *
 * The bit order matches update_display() exactly, because it has to: bytes go
 * out from _buffer[5] down to _buffer[0], so that _buffer[0] ends up in the last
 * register of the chain and reads as the leftmost digit, and each byte goes out
 * LSB first, with the decimal point in bit 0.
 *
 * A digit is lit in a slice when the slice index is below its level, so a level
 * of k lights it in slices 0..k-1 -- k of SHIFT_ENGINE_SLICES, which is exactly
 * k/N of the light. Which slices they are does not matter, only how many, because
 * every slice spans one whole ~OE period and so carries the same on-time.
 */
void ShiftDisplay::build_waveform()
{
    // Resolve each character to its segments once rather than once per slice.
    // Eight lookups per digit would all return the same byte, and this runs
    // while the DMA is reading the buffer it rewrites -- the shorter that
    // window, the less there is to tear.
    uint8_t patterns[_char_buffer_size];

    for (uint8_t d = 0; d < _char_buffer_size; d++)
    {
        patterns[d] = map_ascii(_buffer[d]);
        if (GET_BIT(_dp_state, d))
        {
            SET_BIT(patterns[d], DP_BM);
        }
    }

    for (uint8_t slice = 0; slice < SHIFT_ENGINE_SLICES; slice++)
    {
        uint32_t *words = &_wave[(uint32_t)slice * SHIFT_ENGINE_WORDS_PER_SLICE];
        uint8_t w = 0;

        for (int8_t d = _char_buffer_size - 1; d >= 0; d--)
        {
            // Lit in this slice, or dark: how many slices a digit appears in is
            // its brightness.
            uint8_t pattern = (slice < _digit_levels[d]) ? patterns[d] : 0;

            for (uint8_t mask = 0x01; mask != 0; mask <<= 1, w++)
            {
                // Present the bit and hold the clock low. Only the driver's own
                // pins are named, so ~OE is untouched whatever the word says.
                words[w] = ((pattern & mask) ? _data_set : _data_clr) | _clk_clr;
            }
        }

        // The latch pulse rides in the first two words. Placing it here rather
        // than testing for it inside the loop saves two comparisons on each of
        // the 384 words to position two bits.
        words[0] |= _latch_set;
        words[1] |= _latch_clr;
    }
}

void ShiftDisplay::setDigitLevel(uint8_t index, uint8_t slices)
{
    if (index >= _char_buffer_size)
    {
        return;
    }

    _digit_levels[index] = (slices > SHIFT_ENGINE_SLICES) ? SHIFT_ENGINE_SLICES : slices;
    build_waveform();
}

uint8_t ShiftDisplay::getDigitLevel(uint8_t index) const
{
    return (index < _char_buffer_size) ? _digit_levels[index] : 0;
}

void ShiftDisplay::setAllDigitLevels(uint8_t slices)
{
    uint8_t level = (slices > SHIFT_ENGINE_SLICES) ? SHIFT_ENGINE_SLICES : slices;

    for (uint8_t i = 0; i < _char_buffer_size; i++)
    {
        _digit_levels[i] = level;
    }

    build_waveform();
}

/**
 * Hand the display to TIM1 and two DMA channels.
 *
 * The trigger pairing differs from the one the design first named, and the
 * reason is worth keeping. The design had CH1 present the data and CH2 raise the
 * clock, which needs CCR1 = 0 -- a compare that coincides with the counter wrap,
 * and an edge case not worth betting a hard-to-debug peripheral on. The update
 * event *is* the wrap, unambiguously, so the data rides on update and only the
 * clock needs a compare, at mid-period where nothing is ambiguous.
 *
 *   TIM1 update  -> DMA1 ch5 -> GPIOA->BSRR   48 words per slice, incrementing
 *   TIM1 CC1     -> DMA1 ch2 -> GPIOA->BSRR   one constant word, no increment
 *
 * Both channels are otherwise free: ch5 also serves USART1_RX and TIM2_CH1, ch2
 * serves USART3_TX, SPI1_RX and TIM2_UP, and none of those is in this build.
 *
 * No pin is handed to an alternate function. SER, SRCLK and RCLK stay plain GPIO
 * outputs and the DMA writes BSRR, which is the whole reason this approach works
 * on a board whose shift registers are not wired to SPI. CC1E stays clear so
 * PA8, TIM1_CH1's pin, is never driven.
 *
 * One clock edge fires before the first data word, because CC1 at mid-period
 * comes before the first wrap. It is harmless: the chain is exactly 48 bits, so
 * a leading garbage bit is pushed out by the time the first latch happens.
 */
void ShiftDisplay::startEngine()
{
    if (!_initialized || !_engine_capable || _engine_running)
    {
        return;
    }

    // Every timing constant was derived from an assumed core clock. If the
    // running system disagrees, the bit clock and the slice-to-~OE lock are both
    // wrong, and a blank display is the honest outcome.
    if (!shift_engine_clock_ok())
    {
        return;
    }

    build_waveform();

    // Idle the clock and latch low before the engine takes over.
    _data_port->BSRR = _clk_clr | _latch_clr;

    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_TIM1_CLK_ENABLE();

    // Data: one word per bit, walking the waveform, wrapping forever.
    DMA1_Channel5->CCR = 0;
    DMA1_Channel5->CPAR = (uint32_t)&_data_port->BSRR;
    DMA1_Channel5->CMAR = (uint32_t)_wave;
    DMA1_Channel5->CNDTR = SHIFT_ENGINE_WORDS_TOTAL;
    DMA1_Channel5->CCR = DMA_CCR_DIR | DMA_CCR_CIRC | DMA_CCR_MINC |
                         DMA_CCR_PSIZE_1 | DMA_CCR_MSIZE_1 | DMA_CCR_PL_1;

    // Clock: the same word every time, so no memory increment and four bytes of
    // source for any buffer length.
    DMA1_Channel2->CCR = 0;
    DMA1_Channel2->CPAR = (uint32_t)&_data_port->BSRR;
    DMA1_Channel2->CMAR = (uint32_t)&_clk_high_word;
    DMA1_Channel2->CNDTR = SHIFT_ENGINE_WORDS_TOTAL;
    DMA1_Channel2->CCR = DMA_CCR_DIR | DMA_CCR_CIRC |
                         DMA_CCR_PSIZE_1 | DMA_CCR_MSIZE_1 | DMA_CCR_PL_1;

    DMA1_Channel5->CCR |= DMA_CCR_EN;
    DMA1_Channel2->CCR |= DMA_CCR_EN;

    TIM1->CR1 = 0;
    TIM1->CR2 = 0; // CCDS clear: CC1 requests come from the CC event, not update
    TIM1->PSC = 0;
    TIM1->ARR = SHIFT_ENGINE_ARR;
    TIM1->CCR1 = SHIFT_ENGINE_CCR_CLOCK;
    TIM1->CCER = 0; // no output pin is driven

    // Load PSC and ARR, then clear the flags that generated, before enabling the
    // DMA requests -- otherwise this update event fires a transfer of its own.
    TIM1->EGR = TIM_EGR_UG;
    TIM1->SR = 0;

    TIM1->DIER = TIM_DIER_UDE | TIM_DIER_CC1DE;
    TIM1->CR1 = TIM_CR1_CEN;

    _engine_running = true;
}

void ShiftDisplay::stopEngine()
{
    if (!_engine_running)
    {
        return;
    }

    TIM1->CR1 &= ~TIM_CR1_CEN;
    TIM1->DIER = 0;
    DMA1_Channel5->CCR &= ~DMA_CCR_EN;
    DMA1_Channel2->CCR &= ~DMA_CCR_EN;

    // Leave the pins where the bit-banged path expects to find them.
    _data_port->BSRR = _clk_clr | _latch_clr;

    _engine_running = false;
}

void ShiftDisplay::shiftSliceByHand(uint8_t slice)
{
    if (!_initialized || !_engine_capable || slice >= SHIFT_ENGINE_SLICES)
    {
        return;
    }

    const uint32_t *words = &_wave[(uint32_t)slice * SHIFT_ENGINE_WORDS_PER_SLICE];

    for (uint8_t w = 0; w < SHIFT_ENGINE_WORDS_PER_SLICE; w++)
    {
        // What the data channel writes on the CH1 compare.
        _data_port->BSRR = words[w];
        SHIFT_EDGE_DELAY();

        // What the clock channel writes on the CH2 compare.
        _data_port->BSRR = _clk_high_word;
        SHIFT_EDGE_DELAY();
    }

    _data_port->BSRR = _clk_clr;
}

void ShiftDisplay::write_output_enable(uint16_t brightness)
{
    if (brightness > _max_duty)
    {
        brightness = _max_duty;
    }

    // ~OE high blanks the outputs, so full brightness is zero duty on the pin.
    analogWrite(_output_en_pin, _max_duty - brightness);
}

void ShiftDisplay::setBrightness(uint16_t duty)
{
    _brightness = (duty > _max_duty) ? _max_duty : duty;
    write_output_enable(_brightness);
}

/**
 * True if any pin this driver drives shares a port bit with ~OE.
 *
 * Every word the driver composes -- and every word the DMA engine will replay
 * from a buffer -- is built from these four masks and nothing else, so if none
 * of them touches the ~OE bit on the ~OE port, no word can. Checking the masks
 * once is therefore equivalent to checking every word, and far cheaper.
 *
 * Both halves of BSRR are covered: the clear words are the set words shifted up
 * 16, so a mask clear of the ~OE bit is clear in both halves by construction.
 */
bool ShiftDisplay::oe_collides() const
{
    struct
    {
        GPIO_TypeDef *port;
        uint32_t mask;
    } const pins[] = {
        {_data_port, _data_set},
        {_clk_port, _clk_set},
        {_clr_port, _srclr_set},
        {_latch_port, _latch_set},
    };

    for (auto const &p : pins)
    {
        if (p.port == _oe_port && (p.mask & _oe_mask) != 0U)
        {
            return true;
        }
    }

    return false;
}

void ShiftDisplay::begin(uint32_t delay_us, uint16_t max_duty)
{
    pinMode(_serial_data_pin, OUTPUT);
    pinMode(_serial_clk_pin, OUTPUT);
    pinMode(_serial_clr_pin, OUTPUT);
    pinMode(_latch_clk_pin, OUTPUT);

    digitalWrite(_serial_data_pin, LOW);
    digitalWrite(_serial_clk_pin, LOW);
    digitalWrite(_serial_clr_pin, HIGH);
    digitalWrite(_latch_clk_pin, LOW);

    // ~OE belongs to the timer from here on. Nothing may digitalWrite this pin:
    // doing so reconfigures it away from its alternate function and silently
    // stops the brightness control, blanking or stranding the whole display.
    // Resolve each pin to its port and BSRR words once, here, so the inner loop
    // never repeats the lookup. begin() is also where the pins are known to be
    // configured, having just been through pinMode above.
    PinName data = digitalPinToPinName(_serial_data_pin);
    PinName clk = digitalPinToPinName(_serial_clk_pin);
    PinName srclr = digitalPinToPinName(_serial_clr_pin);
    PinName latch = digitalPinToPinName(_latch_clk_pin);

    _data_port = set_GPIO_Port_Clock(STM_PORT(data));
    _clk_port = set_GPIO_Port_Clock(STM_PORT(clk));
    _clr_port = set_GPIO_Port_Clock(STM_PORT(srclr));
    _latch_port = set_GPIO_Port_Clock(STM_PORT(latch));

    _data_set = STM_GPIO_PIN(data);
    _data_clr = _data_set << 16;
    _clk_set = STM_GPIO_PIN(clk);
    _clk_clr = _clk_set << 16;
    _srclr_set = STM_GPIO_PIN(srclr);
    _srclr_clr = _srclr_set << 16;
    _latch_set = STM_GPIO_PIN(latch);
    _latch_clr = _latch_set << 16;

    _same_port = (_data_port == _clk_port);
    _bit1_clklow = _data_set | _clk_clr;
    _bit0_clklow = _data_clr | _clk_clr;

    // Resolve ~OE too -- not to write it, but to prove nothing else can.
    PinName oe = digitalPinToPinName(_output_en_pin);
    _oe_port = set_GPIO_Port_Clock(STM_PORT(oe));
    _oe_mask = STM_GPIO_PIN(oe);
    _pins_safe = !oe_collides();

    // One DMA channel writes one port, and the waveform carries data, clock and
    // latch bits together, so all three have to live on the same port. On this
    // board they do -- PA3, PA4 and PA2 -- but the pin map is a variant's to
    // change, so this is checked rather than assumed.
    _engine_capable = (_data_port == _clk_port) && (_data_port == _latch_port);
    _engine_running = false;

    // The single word the clock channel replays for every rising edge.
    _clk_high_word = _clk_set;

    for (uint8_t i = 0; i < _char_buffer_size; i++)
    {
        _digit_levels[i] = SHIFT_ENGINE_SLICES;
    }

    build_waveform();

    _max_duty = max_duty;
    _brightness = 0;
    write_output_enable(0); // start blank, as the 10k pull-up already does

    _delay_us = delay_us;

    // A driver that would write across ~OE does not come up. Blank is a state
    // someone investigates; a display that dims unpredictably is one they live
    // with and misdiagnose.
    _initialized = _pins_safe;

#if SHIFT_ENGINE_DMA
    // Brightness is still zero here, so the first frame -- which latches whatever
    // the registers powered up holding -- is never visible.
    startEngine();
#endif
}

void ShiftDisplay::update()
{
    if (!_initialized)
    {
        return;
    }

    update_display();
}

uint8_t ShiftDisplay::map_ascii(char ascii)
{
    if (ascii == ' ')
    {
        return 0;
    }

    if (ascii < SEG_FIRST_CHAR || ascii > SEG_LAST_CHAR)
    {
        return SEG_INVALID;
    }

    uint8_t pattern = segment_data[ascii - SEG_FIRST_CHAR];

    // A zero entry means no glyph is defined for this character.
    return pattern ? pattern : SEG_INVALID;
}

void ShiftDisplay::shiftOutByte(uint8_t byte, bool dp)
{
    // The engine owns the pins while it runs; shifting here would fight it.
    if (_engine_running)
    {
        return;
    }

    if (dp)
    {
        SET_BIT(byte, DP_BM);
    }
    if (_same_port)
    {
        for (uint8_t i = 0; i < 8; i++)
        {
            // One store presents the bit and holds the clock low.
            _data_port->BSRR = (byte & (1u << i)) ? _bit1_clklow : _bit0_clklow;
            SHIFT_EDGE_DELAY();
            _data_port->BSRR = _clk_set; // rising edge shifts it in
            SHIFT_EDGE_DELAY();
        }
    }
    else
    {
        for (uint8_t i = 0; i < 8; i++)
        {
            _data_port->BSRR = (byte & (1u << i)) ? _data_set : _data_clr;
            _clk_port->BSRR = _clk_clr;
            SHIFT_EDGE_DELAY();
            _clk_port->BSRR = _clk_set;
            SHIFT_EDGE_DELAY();
        }
    }

    // Leave the clock idling low, as the digitalWrite version did.
    _clk_port->BSRR = _clk_clr;
}

void ShiftDisplay::shiftOutAscii(char ascii, bool dp)
{
    shiftOutByte(map_ascii(ascii), dp);
}

void ShiftDisplay::setContent(const char *buffer, uint8_t dp)
{
    update_buffer(buffer);
    _dp_state = dp;
    build_waveform();
}

void ShiftDisplay::writeDisplay(const char *buffer, uint8_t dp)
{
    setContent(buffer, dp);

    // With the engine running the refresh picks the new words up on its next
    // pass; walking the shift loop as well would spend 48 iterations reaching a
    // guard that returns immediately.
    if (!_engine_running)
    {
        update_display();
    }
}

// enable() restores the configured brightness rather than going to full, so a
// blank/restore cycle cannot quietly undo the user's setting.
void ShiftDisplay::enable()
{
    write_output_enable(_brightness);
}

void ShiftDisplay::disable()
{
    write_output_enable(0);
}

/**
 * Blank the display.
 *
 * This used to mean only "pulse ~SRCLR and latch", which left _buffer holding
 * whatever it held: the registers were empty but the driver still believed it
 * was showing something. Under the engine that meaning does not survive at all,
 * because the next frame re-shifts the buffer over the cleared registers two
 * milliseconds later.
 *
 * So clearing is now a change of content in both modes, and the ~SRCLR pulse is
 * what the bit-banged path additionally does to make it immediate. Same meaning
 * either way, which is the point -- a caller should not have to know which path
 * is compiled in to know what clear() does.
 */
void ShiftDisplay::clear()
{
    for (uint8_t i = 0; i < _char_buffer_size; i++)
    {
        _buffer[i] = ' ';
    }
    _dp_state = 0;

    build_waveform();

    if (_engine_running)
    {
        return;
    }

    // Clear the shift register (~SRCLR is active low)
    _clr_port->BSRR = _srclr_clr;
    SHIFT_EDGE_DELAY();
    _clr_port->BSRR = _srclr_set;
    SHIFT_EDGE_DELAY();

    // Latch the cleared register
    _latch_port->BSRR = _latch_clr;
    SHIFT_EDGE_DELAY();
    _latch_port->BSRR = _latch_set;
    SHIFT_EDGE_DELAY();
}

void ShiftDisplay::latch()
{
    // The waveform latches every slice; a latch from here would land mid-stream.
    if (_engine_running)
    {
        return;
    }

    // Latch the shift register
    _latch_port->BSRR = _latch_clr;
    SHIFT_EDGE_DELAY();
    _latch_port->BSRR = _latch_set;
    SHIFT_EDGE_DELAY();
}

/**
 * Print one character.
 *
 * The bit-banged model is a stream: each character shifted goes into the first
 * register and pushes everything already there one place along, so the newest
 * character lands leftmost. Under the engine there is nothing to push -- the
 * refresh replays a buffer -- so the same effect is produced by moving the
 * buffer along instead. The decimal points travel with their characters.
 *
 * Without this the Print path would not fail, which is worse: it would return
 * success and display nothing, because shiftOutByte() declines to fight the
 * engine for the pins.
 */
inline size_t ShiftDisplay::write(uint8_t value)
{
    if ((char)value == '\r' || (char)value == '\n')
    {
        return 1;
    }

    if (_engine_running)
    {
        for (uint8_t i = _char_buffer_size - 1; i > 0; i--)
        {
            _buffer[i] = _buffer[i - 1];
        }
        _buffer[0] = (char)value;

        // Bit i is digit i's point, so moving every digit one place along is a
        // single shift; the incoming character arrives without a point.
        _dp_state = (uint8_t)((_dp_state << 1) & 0x3F);

        build_waveform();
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
