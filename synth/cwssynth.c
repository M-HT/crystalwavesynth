/**
 *
 *  Copyright (C) 2026 Roman Pauer
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy of
 *  this software and associated documentation files (the "Software"), to deal in
 *  the Software without restriction, including without limitation the rights to
 *  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *  of the Software, and to permit persons to whom the Software is furnished to do
 *  so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 */

#include "cwssynth.h"


#ifdef __cplusplus
extern "C" {
#endif

extern event_buffer_t *cws_event_buffer;

#ifdef __cplusplus
}
#endif


static uint8_t program_number[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static uint16_t rpn_number[16] = { 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff };
static uint16_t rpn_value[16][3] = {
    {0x100, 0x2000, 0x2000},
    {0x100, 0x2000, 0x2000},
    {0x100, 0x2000, 0x2000},
    {0x100, 0x2000, 0x2000},
    {0x100, 0x2000, 0x2000},
    {0x100, 0x2000, 0x2000},
    {0x100, 0x2000, 0x2000},
    {0x100, 0x2000, 0x2000},
    {0x100, 0x2000, 0x2000},
    {0x100, 0x2000, 0x2000},
    {0x100, 0x2000, 0x2000},
    {0x100, 0x2000, 0x2000},
    {0x100, 0x2000, 0x2000},
    {0x100, 0x2000, 0x2000},
    {0x100, 0x2000, 0x2000},
    {0x100, 0x2000, 0x2000}
};
static struct {
    uint8_t note;
    uint8_t channel;
} active_notes[200];
static uint32_t active_num = 0;


static void add_to_buffer(uint32_t time, uint16_t type, uint16_t channel, uint16_t value1, uint16_t value2, uint16_t value3)
{
    uint32_t write_offset, next_offset;
    event_t *event;

    write_offset = cws_event_buffer->write_offset;
    event = (event_t *)(cws_event_buffer->buffer + write_offset);

    next_offset = write_offset + sizeof(event_t);
    if (next_offset >= sizeof(cws_event_buffer->buffer))
    {
        next_offset = 0;
    }

    // WARNING: when the buffer is full, i.e. next_offset == cws_event_buffer->read_offset,
    // then adding an event causes the buffer to become empty and all events in the buffer are lost
    // this is how the original code works

    event->time_low = (uint16_t)time;
    event->time_high = (uint16_t)(time >> 16);
    event->type = type;
    event->channel = channel;
    event->value1 = value1;
    event->value2 = value2;
    event->value3 = value3;

    cws_event_buffer->write_offset = next_offset;
}

static void note_off(uint32_t time, uint32_t channel, uint32_t note)
{
    uint32_t active_index;

    for (active_index = 0; active_index < active_num; active_index++)
    {
        if (active_notes[active_index].channel == channel && active_notes[active_index].note == note)
        {
            for (active_index++; active_index < active_num; active_index++)
            {
                active_notes[active_index - 1] = active_notes[active_index];
            }
            active_num--;

            add_to_buffer(time, 1, channel, note, 0, 0);
            break;
        }
    }
}

static void note_on(uint32_t time, uint32_t channel, uint32_t note, uint32_t velocity)
{
    if (velocity == 0)
    {
        note_off(time, channel, note);
        return;
    }

    // WARNING: following check is not in the original code
    // so, in the original code, when the number of active notes reaches 200, bad things start to happen
    if (active_num >= 200) return;

    active_notes[active_num].note = note;
    active_notes[active_num].channel = channel;
    active_num++;

    add_to_buffer(time, 0, channel, note, velocity, (channel == 9) ? (note + 127) : program_number[channel]);
}

static void all_notes_off(uint32_t time, uint32_t channel)
{
    uint32_t active_index, index2, note;

    for (active_index = 0; active_index < active_num; active_index++)
    {
        if (active_notes[active_index].channel == channel)
        {
            note = active_notes[active_index].note;

            for (index2 = active_index + 1; index2 < active_num; index2++)
            {
                active_notes[index2 - 1] = active_notes[index2];
            }
            active_num--;

            add_to_buffer(time, 1, channel, note, 0, 0);
        }
    }
}

void CCALL CWS_MidiMessageShort(uint32_t time, uint32_t message)
{
    uint32_t channel;

    channel = message & 0x0f;

    switch ((message >> 4) & 0x0f)
    {
        case 8: // note off
            note_off(time, channel, (message >> 8) & 0x7f);
            break;

        case 9: // note on
            note_on(time, channel, (message >> 8) & 0x7f, (message >> 16) & 0x7f);
            break;

        case 11: // controller change
            switch ((message >> 8) & 0x7f)
            {
                case 6: // data entry MSB
                    if (rpn_number[channel] <= 2)
                    {
                        rpn_value[channel][rpn_number[channel]] = (rpn_value[channel][rpn_number[channel]] & 0x7f) | ((message >> 9) & 0x3f80);
                        add_to_buffer(time, 7, channel, rpn_number[channel], rpn_value[channel][rpn_number[channel]], 0);
                    }
                    break;

                case 38: // data entry LSB
                    if (rpn_number[channel] <= 2)
                    {
                        rpn_value[channel][rpn_number[channel]] = (rpn_value[channel][rpn_number[channel]] & 0x3f80) | ((message >> 16) & 0x7f);
                        add_to_buffer(time, 7, channel, rpn_number[channel], rpn_value[channel][rpn_number[channel]], 0);
                    }
                    break;

                case 96: // increase RPN/NRPN value
                    if (rpn_number[channel] <= 2)
                    {
                        rpn_value[channel][rpn_number[channel]]++;
                        add_to_buffer(time, 7, channel, rpn_number[channel], rpn_value[channel][rpn_number[channel]], 0);
                    }
                    break;

                case 97: // decrease RPN/NRPN value
                    if (rpn_number[channel] <= 2)
                    {
                        rpn_value[channel][rpn_number[channel]]--;
                        add_to_buffer(time, 7, channel, rpn_number[channel], rpn_value[channel][rpn_number[channel]], 0);
                    }
                    break;

                // WARNING: NRPN LSB and MSB are not handled in the original code
                // so it would change a RPN value when trying to change NRPN value
                case 98: // NRPN LSB
                case 99: // NRPN MSB
                    rpn_number[channel] = 0x3fff;
                    break;

                case 100: // RPN LSB
                    rpn_number[channel] = (rpn_number[channel] & 0x3f80) | ((message >> 16) & 0x7f);
                    break;

                case 101: // RPN MSB
                    rpn_number[channel] = (rpn_number[channel] & 0x7f) | ((message >> 9) & 0x3f80);
                    break;

                case 121: // reset all controllers
                    rpn_value[channel][0] = 0x100;
                    rpn_value[channel][1] = 0x2000;
                    rpn_value[channel][2] = 0x2000;
                    // fallthrough
                case 7: // volume
                case 10: // pan
                case 11: // expression
                case 64: // sustain pedal
                    add_to_buffer(time, 5, channel, (message >> 8) & 0x7f, (message >> 16) & 0x7f, 0);
                    break;

                case 120: // all sounds off
                case 123: // all notes off
                case 124: // omni off
                case 125: // omni on
                case 126: // mono on
                case 127: // poly on
                    all_notes_off(time, channel);
                    break;

                default:
                    break;
            }
            break;

        case 12: // program change
            program_number[channel] = (message >> 8) & 0x7f;
            break;

        case 14: // pitch bend
            add_to_buffer(time, 6, channel, ((message >> 8) & 0x7f) | ((message >> 9) & 0x3f80), 0, 0);
            break;

        case 15:
            if (channel == 0x0f) // reset
            {
                for (channel = 0; channel < 16; channel++)
                {
                    all_notes_off(time, channel);
                    rpn_number[channel] = 0x3fff; // WARNING: the original code didn't reset RPN/NRPN number when resetting
                    rpn_value[channel][0] = 0x100;
                    rpn_value[channel][1] = 0x2000;
                    rpn_value[channel][2] = 0x2000;
                    add_to_buffer(time, 5, channel, 121, 0, 0);
                }
            }
            break;

        default:
            break;
    }
}

