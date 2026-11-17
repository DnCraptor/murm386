// https://www.recordingblogs.com/wiki/midi-voice-messages
// https://midi.org/summary-of-midi-1-0-messages
// https://www.blitter.com/~russtopia/MIDI/~jglatt/tech/midispec/messages.htm
// https://newt.phys.unsw.edu.au/jw/notes.html
// https://massimo-nazaria.github.io/midi-synth.html
#include <math.h>
#define INLINE __always_inline
#define EMULATED_MIDI

// #define DEBUG_MPU401 1

enum { STATUS_READY = 0, STATUS_OUTPUT_NOT_READY = 0x40, STATUS_INPUT_NOT_READY = 0x80 };

uint8_t mpu_status = STATUS_INPUT_NOT_READY;
uint8_t mpu_rx_data = 0;

static int midi_pos, midi_len;
static uint32_t midi_command;
static const uint8_t midi_channel_lengths[] = {3, 3, 3, 3, 2, 2, 3};
static const uint8_t midi_system_lengths[] = {0, 2, 3, 2, 0, 0, 1, 0};
static uint8_t midi_running_status;
static int midi_insysex;


#if defined(EMULATED_MIDI)
#include <general-midi.c.inl>
#if defined(MIDI_GM_DLS)
#include "midi/gm_synth.c.inl"
#endif
#else

static int midi_id = 0;
static HMIDIOUT midi_out_device = NULL;

static INLINE void midi_init_once() {
    if (midi_out_device != NULL) return;

    MMRESULT hr = midiOutOpen(&midi_out_device, midi_id, 0, 0, CALLBACK_NULL);
    if (hr != MMSYSERR_NOERROR) {
#ifdef DEBUG_MPU401
        printf("midiOutOpen error - %08X\n", hr);
#endif
        return;
    }
    midiOutReset(midi_out_device);
}

static INLINE void midi_close() {
    if (midi_out_device != NULL) {
        midiOutReset(midi_out_device);
        midiOutClose(midi_out_device);
        midi_out_device = NULL;
    }
}

static uint8_t midi_sysex_data[4096 + 2];
static INLINE void midi_send_sysex() {
    MIDIHDR hdr = {
        .lpData = (LPSTR) midi_sysex_data,
        .dwBufferLength = midi_pos,
        .dwFlags = 0
    };
    // Prepare the MIDI header for sending
    MMRESULT result = midiOutPrepareHeader(midi_out_device, &hdr, sizeof(MIDIHDR));
    if (result != MMSYSERR_NOERROR) {
#ifdef DEBUG_MPU401
        printf("Error preparing SysEx header: %08X\n", result);
#endif
        return;
    }

    // Send the SysEx message
    result = midiOutLongMsg(midi_out_device, &hdr, sizeof(MIDIHDR));
    if (result != MMSYSERR_NOERROR) {
#ifdef DEBUG_MPU401
        printf("Error sending SysEx message: %08X\n", result);
#endif
        midiOutUnprepareHeader(midi_out_device, &hdr, sizeof(MIDIHDR));
        return;
    }

    // Wait for the message to be sent
    while (!(hdr.dwFlags & MHDR_DONE)) {
        Sleep(1); // Allow time for the message to finish sending
    }

    // Unprepare the header after sending
    result = midiOutUnprepareHeader(midi_out_device, &hdr, sizeof(MIDIHDR));
    if (result != MMSYSERR_NOERROR) {
#ifdef DEBUG_MPU401
        printf("Error unpreparing SysEx header: %08X\n", result);
#endif
    }

    // Reset the SysEx state
    midi_insysex = 0;
}
int16_t midi_sample() { return 0; }


#endif


static INLINE void midi_send_short(uint32_t command) {
#if defined(EMULATED_MIDI)
    const uint8_t status = command;
    if (status < 0xF0) {
        const midi_command_t message = {
            .command = status,
            .note = command >> 8,
            .velocity = command >> 16,
        };
#if defined(MIDI_GM_DLS)
        if (gm_sd_bank.header) {
            gm_midi_enqueue(&message);
            return;
        }
#endif
        parse_midi(&message);
    }
#else
    midiOutShortMsg(midi_out_device, command);
#endif
}

static INLINE uint8_t mpu401_read(uint16_t portnum) {
    if (portnum & 1) {
        return mpu_status;
    }

    mpu_status = STATUS_INPUT_NOT_READY;
    return mpu_rx_data;
}

static INLINE void mpu401_write(uint16_t portnum, uint8_t value) {
    if (portnum & 1) {
        switch (value) {
            case 0xDF:
                mpu_rx_data = 0xFE; // Ack
#if !defined(EMULATED_MIDI)
                midi_send_sysex();
#endif
                break;
            case 0xD0: // Enter UART Mode
                mpu_rx_data = 0xFF; // ???
                break;
            case 0x3F: // Enter Intellegent UART mode
                mpu_rx_data = 0xFE; // Ack
                break;
            default:
#ifdef DEBUG_MPU401
                printf("[MIDI] Unknown %x %x\n", portnum, value);
#endif
                mpu_rx_data = 0xFE;
                break;
            case 0xFF: // Reset
                midi_pos = midi_len = midi_insysex = 0;
                midi_running_status = 0;
                midi_command = 0;
#if !defined(EMULATED_MIDI)
                midi_init_once();
#elif defined(MIDI_GM_DLS)
                if (gm_sd_bank.header) {
                    const midi_command_t reset = {.command = 0xff};
                    gm_midi_enqueue(&reset);
                }
#endif
                mpu_rx_data = 0xFE; // Ack
                break;
        }

        mpu_status = STATUS_READY;
        return;
    }

    /* Realtime bytes can occur anywhere, including inside SysEx. */
    if (value >= 0xF8) {
        midi_send_short(value);
        return;
    }

    if (midi_insysex) {
        if (value < 0x80 || value == 0xF7) {
#if !defined(EMULATED_MIDI)
            if (midi_pos < sizeof(midi_sysex_data))
                midi_sysex_data[midi_pos++] = value;
            if (value == 0xF7 && midi_pos < sizeof(midi_sysex_data))
                midi_send_sysex();
#endif
            if (value == 0xF7)
                midi_insysex = 0;
            return;
        }
        midi_insysex = 0;
    }

    if (value & 0x80) {
        midi_pos = 0;
        midi_command = value;
        if (value < 0xF0) {
            midi_running_status = value;
            midi_len = midi_channel_lengths[(value >> 4) - 8];
        } else {
            midi_running_status = 0;
            midi_len = midi_system_lengths[value - 0xF0];
            if (value == 0xF0) {
                midi_insysex = 1;
#if !defined(EMULATED_MIDI)
                midi_sysex_data[midi_pos++] = value;
#endif
                return;
            }
        }
        if (midi_len == 1) {
            midi_send_short(midi_command);
            midi_len = 0;
        } else if (midi_len > 1) {
            midi_pos = 1;
        }
        return;
    }

    if (!midi_len) {
        if (!midi_running_status)
            return;
        midi_command = midi_running_status;
        midi_len = midi_channel_lengths[(midi_running_status >> 4) - 8];
        midi_pos = 1;
    }

    midi_command |= (uint32_t)value << (midi_pos * 8);
    if (++midi_pos == midi_len) {
        midi_send_short(midi_command);
        midi_len = 0;
    }
}
