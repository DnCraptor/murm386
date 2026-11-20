/* Small, allocation-free, stereo PCM oscillator for WINN's native FGEN.EXE.
 * This file deliberately has no WINN dependencies; it is also host-testable. */
#ifndef FGEN_SYNTH_H
#define FGEN_SYNTH_H
#include <stdint.h>

#define FGEN_RATE 44100u
#define FGEN_CHANNELS 2u
#define FGEN_BITS 16u
#define FGEN_BLOCK 4096u

enum { FGEN_SINE, FGEN_SQUARE, FGEN_SAW, FGEN_PULSE, FGEN_WAVES };

typedef struct {
    uint32_t phase;
    uint32_t step;
    uint16_t gain[2]; /* 0..32767, per-channel Q15 */
    uint8_t waveform[2];
} fgen_synth;

void fgen_synth_init(fgen_synth *s);
void fgen_synth_frequency(fgen_synth *s, unsigned hz);
void fgen_synth_render(fgen_synth *s, uint8_t *pcm, unsigned frames);
#endif
