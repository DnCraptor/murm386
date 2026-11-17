#include <stdio.h>

#include "board_config.h"
#include "fdos/psram_layout.h"
#include "ff.h"
#include "gm_bank.h"
#include "psram_init.h"

gm_bank_view_t gm_sd_bank;

static bool table_fits(uint32_t offset, uint32_t count, size_t entry_size,
                       uint32_t begin, uint32_t end)
{
    return !(offset & 3u) && offset >= begin && offset <= end &&
           count <= (end - offset) / entry_size;
}

static bool gm_validate(const void *data, uint32_t size, gm_bank_view_t *bank)
{
    const gm_bank_header_t *h = data;
    if (size < sizeof(*h) || (h->output_rate != 22050 && h->output_rate != 44100) ||
        !h->instrument_count || !h->region_count || !h->wave_count ||
        h->off_instruments > h->off_regions || h->off_regions > h->off_waves ||
        h->off_waves > h->off_pcm ||
        !table_fits(h->off_pcm, h->pcm_samples, sizeof(int16_t), sizeof(*h), size) ||
        !table_fits(h->off_instruments, h->instrument_count, sizeof(gm_instrument_t),
                    sizeof(*h), h->off_regions) ||
        !table_fits(h->off_regions, h->region_count, sizeof(gm_region_t),
                    sizeof(*h), h->off_waves) ||
        !table_fits(h->off_waves, h->wave_count, sizeof(gm_wave_t),
                    sizeof(*h), h->off_pcm) ||
        !gm_bank_view(data, bank))
        return false;

    for (uint32_t i = 0; i < h->instrument_count; ++i) {
        const gm_instrument_t *ins = &bank->instruments[i];
        if (ins->region_first > h->region_count ||
            ins->region_count > h->region_count - ins->region_first)
            return false;
    }
    for (uint32_t i = 0; i < h->wave_count; ++i) {
        const gm_wave_t *wave = &bank->waves[i];
        if (wave->pcm_offset > h->pcm_samples ||
            wave->frame_count > h->pcm_samples - wave->pcm_offset)
            return false;
    }
    for (uint32_t i = 0; i < h->region_count; ++i) {
        const gm_region_t *r = &bank->regions[i];
        if (r->wave_index >= h->wave_count)
            return false;
        const uint32_t frames = bank->waves[r->wave_index].frame_count;
        if ((r->flags & GM_RGN_LOOPED) &&
            (!r->loop_length || r->loop_start >= frames ||
             r->loop_length > frames - r->loop_start))
            return false;
    }
    return true;
}

void gm_midi_load_sd(void)
{
    static const char *const dirs[] = {
        "/" CONFIG_BOARD_DIR, "/" CONFIG_CPU_DIR, "/" CONFIG_ROOT_DIR,
        "/.config/speccy", "/.config/spec",
        "/.config/pc-286", "/.config/286", "/.config/pc",
        "/config/speccy", "/config/spec", "/config/pc-286", "/config/286", "/config/pc",
        "/speccy", "/spec", "/pc-286", "/286", "/xt", "",
    };
    static const char *const names[] = {
        "gm_bank_22050.bin", "gm_bank.bin", "gm_bank_44100.bin"
    };
    const size_t capacity = psram_usable_size();
    size_t min_remaining = (1u << 20) + ARM_ELF_NATIVE_STACK_ARENA_SIZE;
#if EMULATE_LTEMS
    // Keep enough RAM to continue exposing the existing EMS board.
    if (capacity >= (4u << 20))
        min_remaining = 4u << 20;
#endif
    if (capacity <= min_remaining)
        return;

    FIL file;
    for (unsigned dir = 0; dir < sizeof(dirs) / sizeof(dirs[0]); ++dir) {
        for (unsigned name = 0; name < sizeof(names) / sizeof(names[0]); ++name) {
            char path[80];
            const int path_size = snprintf(path, sizeof(path), "%s/%s", dirs[dir], names[name]);
            if (path_size < 0 || (size_t)path_size >= sizeof(path) ||
                f_open(&file, path, FA_READ) != FR_OK)
                continue;

            const FSIZE_t size = f_size(&file);
            if (size < sizeof(gm_bank_header_t) || size > capacity - min_remaining) {
                f_close(&file);
                printf("MIDI: invalid size or insufficient PSRAM for %s\n", path);
                continue;
            }
            // Keep the synth state alongside the bank, above the guest and its caches.
            const size_t workspace_offset = ((size_t)size + 7u) & ~(size_t)7u;
            const size_t reserved =
                (workspace_offset + gm_midi_workspace_size() + 1023u) & ~(size_t)1023u;
            if (reserved > capacity - min_remaining) {
                f_close(&file);
                printf("MIDI: insufficient PSRAM for %s and synth state\n", path);
                continue;
            }

            uint8_t *data = (uint8_t *)((uintptr_t)PSRAM_BASE_ADDR + capacity - reserved);
            UINT read;
            const FRESULT result = f_read(&file, data, (UINT)size, &read);
            f_close(&file);

            gm_bank_view_t bank;
            if (result != FR_OK || read != size || !gm_validate(data, (uint32_t)size, &bank)) {
                printf("MIDI: invalid GMWB v4 bank %s\n", path);
                continue;
            }
            psram_reserve_top(reserved);
            gm_midi_init(data + workspace_offset);
            gm_sd_bank = bank;
            printf("MIDI: %s (%lu Hz, %lu KiB PSRAM)\n", path,
                   (unsigned long)bank.header->output_rate, (unsigned long)(reserved >> 10));
            return;
        }
    }
}
