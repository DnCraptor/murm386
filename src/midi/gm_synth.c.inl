#include "gm_bank.h"
#include "wavetable.c.inl"

enum { GM_MIDI_QUEUE_LENGTH = 128, GM_MIDI_EVENTS_PER_TICK = 8 };
typedef struct {
    wt_voice_t voices[WT_MAX_VOICES];
    wt_channel_t channels[WT_MIDI_CHANNELS];
    midi_command_t queue[GM_MIDI_QUEUE_LENGTH];
} gm_midi_workspace_t;

static midi_command_t *gm_midi_queue;
static unsigned gm_midi_head, gm_midi_tail;
static unsigned gm_midi_overrun;

size_t gm_midi_workspace_size(void)
{
    return sizeof(gm_midi_workspace_t);
}

void gm_midi_init(void *memory)
{
    gm_midi_workspace_t *workspace = memory;
    g_voices = workspace->voices;
    g_channels = workspace->channels;
    gm_midi_queue = workspace->queue;
    wt_engine_reset();
}

// Producer on the emulation path. An overrun requests a synth reset to
// release voices that could otherwise lose their note-off.
static void gm_midi_enqueue(const midi_command_t *message)
{
    const unsigned head = __atomic_load_n(&gm_midi_head, __ATOMIC_RELAXED);
    const unsigned tail = __atomic_load_n(&gm_midi_tail, __ATOMIC_ACQUIRE);
    if (head - tail == GM_MIDI_QUEUE_LENGTH) {
        __atomic_store_n(&gm_midi_overrun, 1, __ATOMIC_RELEASE);
        return;
    }
    gm_midi_queue[head % GM_MIDI_QUEUE_LENGTH] = *message;
    __atomic_store_n(&gm_midi_head, head + 1, __ATOMIC_RELEASE);
}

bool __not_in_flash_func(gm_midi_mix)(int *left, int *right)
{
    if (!gm_sd_bank.header)
        return false;

    static bool repeat;
    static int16_t held_l, held_r;

    if (!repeat) {
        const unsigned head = __atomic_load_n(&gm_midi_head, __ATOMIC_ACQUIRE);
        unsigned tail = __atomic_load_n(&gm_midi_tail, __ATOMIC_RELAXED);
        if (__atomic_exchange_n(&gm_midi_overrun, 0, __ATOMIC_ACQ_REL)) {
            tail = head;
            wt_engine_reset();
        }

        for (unsigned i = 0; i < GM_MIDI_EVENTS_PER_TICK && tail != head; ++i, ++tail) {
            const midi_command_t *message = &gm_midi_queue[tail % GM_MIDI_QUEUE_LENGTH];
            if (message->command == 0xff)
                wt_engine_reset();
            else
                wt_parse_midi(message);
        }
        __atomic_store_n(&gm_midi_tail, tail, __ATOMIC_RELEASE);
        midi_sample_stereo(&held_l, &held_r);
        repeat = gm_sd_bank.header->output_rate == 22050;
    } else {
        repeat = false;
    }

    *left += held_l;
    *right += held_r;
    return true;
}
