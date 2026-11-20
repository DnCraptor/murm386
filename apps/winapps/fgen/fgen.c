/* FGEN.EXE - native two-channel function generator for murm386 WINN.
 *
 * Independent implementation of the four signal shapes and controls used
 * by the historical Marchand Function Generator.  No original executable
 * code or binary data is incorporated.  The waveform is synthesized on ARM
 * in small PCM blocks and submitted to WINN's installed wave driver.
 */
#include "windows.h"
#include "mmsystem.h"
#include "wave_driver.h"
#include "synth.h"
#include <stdint.h>
#include <stddef.h>

#define TIMER_AUDIO 1u
#define ID_FREQUENCY 100u
#define ID_FREQUENCY_SCROLL 101u
#define ID_AMP_L 110u
#define ID_AMP_R 111u
#define ID_RUN 120u
#define ID_WAVE_BASE 130u
#define WAVES_PER_SIDE 4u

static HWND main_window;
static HWND frequency_edit, frequency_scroll;
static HWND amp_scroll[2], amp_label[2], run_button;
static HWND shape_button[2][FGEN_WAVES];
static unsigned frequency_hz = 440u;
static unsigned amplitude[2] = { 25u, 25u };
static unsigned waveform[2] = { FGEN_SINE, FGEN_SINE };
static fgen_synth oscillator;

static HMODULE wave_module, mixer_module;
static const win_wave_driver *wave_driver;
static WAVEOUTGETVOLUME get_wave_volume;
static AUXGETVOLUME get_master_volume;
static uint8_t pcm[FGEN_BLOCK];

static const char *const shape_names[FGEN_WAVES] = {
    "Sine", "Square", "Sawtooth", "Pulse"
};

static unsigned clamp(unsigned n, unsigned lo, unsigned hi)
{
    return n < lo ? lo : n > hi ? hi : n;
}

static unsigned frequency_from_slider(unsigned pos)
{
    /* 20..20000 Hz on an ordinary Win16 scrollbar (0..1000).
     * The edit box permits single-Hz steps at low frequencies. */
    return 20u + (clamp(pos, 0u, 1000u) * 19980u + 500u) / 1000u;
}

static void set_frequency(unsigned value, int from_scroll)
{
    frequency_hz = clamp(value, 0u, 20000u);
    fgen_synth_frequency(&oscillator, frequency_hz);
    if (!from_scroll)
        SetScrollPos(frequency_scroll, SB_CTL,
                     frequency_hz < 20u ? 0 : (int)((frequency_hz - 20u) * 1000u / 19980u), TRUE);
    char buf[16];
    wsprintf(buf, "%u", frequency_hz);
    SetWindowText(frequency_edit, buf);
}

static unsigned parse_frequency(void)
{
    char buf[20];
    GetWindowText(frequency_edit, buf, sizeof(buf));
    unsigned n = 0, any = 0;
    for (const char *p = buf; *p; ++p) {
        if (*p < '0' || *p > '9')
            return frequency_hz;
        any = 1;
        n = n * 10u + (unsigned)(*p - '0');
        if (n > 20000u)
            return 20000u;
    }
    return any ? n : frequency_hz;
}

static void set_amplitude(unsigned channel, unsigned value)
{
    amplitude[channel] = clamp(value, 0u, 100u);
    SetScrollPos(amp_scroll[channel], SB_CTL, (int)amplitude[channel], TRUE);
    char buf[20];
    wsprintf(buf, "%u %%", amplitude[channel]);
    SetWindowText(amp_label[channel], buf);
}

static void set_waveform(unsigned channel, unsigned value)
{
    waveform[channel] = value;
    oscillator.waveform[channel] = (uint8_t)value;
    for (unsigned i = 0; i < FGEN_WAVES; ++i) {
        char buf[24];
        wsprintf(buf, "%s%s", i == value ? "[x] " : "[ ] ", shape_names[i]);
        SetWindowText(shape_button[channel][i], buf);
    }
}

static void mixer_load(void)
{
    mixer_module = LoadLibrary("MMSYSTEM.DLL");
    if (mixer_module < 32u) {
        mixer_module = 0;
        return;
    }
    get_wave_volume = (WAVEOUTGETVOLUME)(void (*)(void))
        GetProcAddress(mixer_module, "waveOutGetVolume");
    get_master_volume = (AUXGETVOLUME)(void (*)(void))
        GetProcAddress(mixer_module, "auxGetVolume");
}

static void mixer_unload(void)
{
    if (mixer_module)
        FreeLibrary(mixer_module);
    mixer_module = 0;
    get_wave_volume = NULL;
    get_master_volume = NULL;
}

/* Same global Master/Wave gain that MMSYSTEM applies to ordinary waveOut.
 * Unlike the old EXE, this program NEVER changes the global Wave volume. */
static void mixer_update(void)
{
    DWORD w = 0xffffffffu, m = 0xffffffffu;
    if (get_wave_volume && get_wave_volume(0, &w))
        w = 0xffffffffu;
    if (get_master_volume && get_master_volume(0, &m))
        m = 0xffffffffu;
    for (unsigned ch = 0; ch < 2; ++ch) {
        unsigned wc = ch ? w >> 16 : w & 0xffffu;
        unsigned mc = ch ? m >> 16 : m & 0xffffu;
        uint32_t gain = amplitude[ch] * 32767u / 100u;
        gain = gain * wc >> 16;
        gain = gain * mc >> 16;
        oscillator.gain[ch] = (uint16_t)gain;
    }
}

static void wave_close(void)
{
    if (wave_driver) {
        wave_driver->stop();
        wave_driver->close();
        wave_driver = NULL;
    }
    if (wave_module)
        FreeLibrary(wave_module);
    wave_module = 0;
}

static int wave_open(void)
{
    char name[64];
    if (!GetPrivateProfileString("drivers", "wave", "", name,
                                 sizeof(name), "SYSTEM.INI") || !name[0])
        return 0;
    wave_module = LoadLibrary(name);
    if (wave_module < 32u) {
        wave_module = 0;
        return 0;
    }
    win_wave_driver_entry entry = (win_wave_driver_entry)(void (*)(void))
        GetProcAddress(wave_module, "WinWaveDriver");
    const win_wave_driver *d = entry ? entry() : NULL;
    if (!d || d->version != WIN_WAVE_DRIVER_VERSION ||
        d->size < sizeof(*d) || !d->present || !d->open || !d->close ||
        !d->submit || !d->finished || !d->stop || !d->room ||
        d->max_bytes < FGEN_BLOCK || !d->present() || !d->open()) {
        wave_close();
        return 0;
    }
    wave_driver = d;
    return 1;
}

static void wave_pump(void)
{
    if (!wave_driver)
        return;
    (void)wave_driver->finished(); /* acknowledge completed DMA transfers */
    mixer_update();
    /* Up to two 4096-byte blocks fit into SBWAVE's 8192-byte DMA ring.
     * Generate them incrementally: no whole-second buffer, no IDIV loop. */
    for (unsigned i = 0; i < 4u && wave_driver->room() > 0; ++i) {
        fgen_synth_render(&oscillator, pcm, FGEN_BLOCK / 4u);
        if (!wave_driver->submit(pcm, FGEN_BLOCK, FGEN_RATE,
                                 FGEN_BITS, FGEN_CHANNELS))
            break;
    }
}

static void set_running(int on)
{
    if (on) {
        if (!wave_driver && !wave_open()) {
            MessageBox(main_window,
                       "Cannot open installed Wave driver.\n"
                       "Check SYSTEM.INI [drivers] wave=... and SB16 availability.",
                       "Marchand Function Generator", MB_OK | MB_ICONEXCLAMATION);
            return;
        }
        SetWindowText(run_button, "&Stop");
        wave_pump();
    } else {
        wave_close();
        SetWindowText(run_button, "&Start");
    }
}

static void scroll_changed(HWND child, UINT code, LPARAM lp)
{
    unsigned id = (unsigned)GetDlgCtrlID(child);
    int is_frequency = id == ID_FREQUENCY_SCROLL;
    int is_amplitude = id == ID_AMP_L || id == ID_AMP_R;
    if (!is_frequency && !is_amplitude)
        return;
    int max = is_frequency ? 1000 : 100;
    int pos = GetScrollPos(child, SB_CTL);
    switch (code) {
    case SB_LINEUP: pos--; break;
    case SB_LINEDOWN: pos++; break;
    case SB_PAGEUP: pos -= is_frequency ? 10 : 5; break;
    case SB_PAGEDOWN: pos += is_frequency ? 10 : 5; break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: pos = (int16_t)LOWORD(lp); break;
    case SB_TOP: pos = 0; break;
    case SB_BOTTOM: pos = max; break;
    default: return;
    }
    pos = (int)clamp(pos < 0 ? 0 : (unsigned)pos, 0u, (unsigned)max);
    SetScrollPos(child, SB_CTL, pos, TRUE);
    if (is_frequency)
        set_frequency(frequency_from_slider((unsigned)pos), 1);
    else
        set_amplitude(id == ID_AMP_R, (unsigned)pos);
}

static LRESULT CALLBACK FGenProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        return 0;
    case WM_TIMER:
        if (wp == TIMER_AUDIO)
            wave_pump();
        return 0;
    case WM_HSCROLL:
        scroll_changed((HWND)HIWORD(lp), wp, lp);
        return 0;
    case WM_COMMAND:
        if (wp == ID_RUN) {
            set_running(!wave_driver);
            return 0;
        }
        if (wp == ID_FREQUENCY && HIWORD(lp) == EN_KILLFOCUS) {
            set_frequency(parse_frequency(), 0);
            return 0;
        }
        if (wp >= ID_WAVE_BASE && wp < ID_WAVE_BASE + 2u * FGEN_WAVES) {
            unsigned index = (unsigned)wp - ID_WAVE_BASE;
            set_waveform(index / FGEN_WAVES, index % FGEN_WAVES);
            return 0;
        }
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_AUDIO);
        wave_close();
        mixer_unload();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static HWND child(HINSTANCE inst, const char *class_name, const char *title,
                  DWORD style, int x, int y, int w, int h, unsigned id)
{
    return CreateWindow(class_name, title, WS_CHILD | WS_VISIBLE | style,
                        x, y, w, h, main_window, (HMENU)id, inst, NULL);
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)cmd;
    if (!prev) {
        WNDCLASS wc = { 0 };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = FGenProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = "FGenNative";
        if (!RegisterClass(&wc))
            return 1;
    }
    fgen_synth_init(&oscillator);
    main_window = CreateWindow("FGenNative", "Marchand Function Generator",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        550, 330, 0, 0, inst, NULL);
    if (!main_window)
        return 1;

    child(inst, "STATIC", "Frequency:", SS_LEFT, 20, 18, 75, 18, 0);
    frequency_edit = child(inst, "EDIT", "440", WS_BORDER | ES_LEFT,
                           98, 13, 72, 23, ID_FREQUENCY);
    child(inst, "STATIC", "Hz", SS_LEFT, 176, 18, 24, 16, 0);
    frequency_scroll = child(inst, "SCROLLBAR", "", SBS_HORZ,
                             206, 16, 304, 18, ID_FREQUENCY_SCROLL);
    SetScrollRange(frequency_scroll, SB_CTL, 0, 1000, FALSE);
    set_frequency(frequency_hz, 0);

    for (unsigned ch = 0; ch < 2; ++ch) {
        int x = ch ? 280 : 20;
        child(inst, "STATIC", ch ? "Right channel" : "Left channel",
              SS_LEFT, x, 57, 210, 18, 0);
        for (unsigned wave = 0; wave < FGEN_WAVES; ++wave)
            shape_button[ch][wave] = child(inst, "BUTTON", shape_names[wave],
                BS_PUSHBUTTON | WS_TABSTOP, x, 80 + (int)wave * 29,
                210, 25, ID_WAVE_BASE + ch * FGEN_WAVES + wave);
        set_waveform(ch, waveform[ch]);
        child(inst, "STATIC", "Amplitude:", SS_LEFT, x, 205, 84, 18, 0);
        amp_label[ch] = child(inst, "STATIC", "", SS_RIGHT,
                              x + 167, 205, 42, 18, 0);
        amp_scroll[ch] = child(inst, "SCROLLBAR", "", SBS_HORZ,
                               x, 227, 210, 18, ch ? ID_AMP_R : ID_AMP_L);
        SetScrollRange(amp_scroll[ch], SB_CTL, 0, 100, FALSE);
        set_amplitude(ch, amplitude[ch]);
    }
    run_button = child(inst, "BUTTON", "&Start",
                       BS_PUSHBUTTON | WS_TABSTOP, 214, 257, 102, 25, ID_RUN);

    mixer_load();
    if (!SetTimer(main_window, TIMER_AUDIO, 10, NULL)) {
        MessageBox(main_window, "Cannot start audio timer.",
                   "Function Generator", MB_OK | MB_ICONEXCLAMATION);
    }
    ShowWindow(main_window, show);
    UpdateWindow(main_window);
    set_running(1);
    MSG m;
    while (GetMessage(&m, 0, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return (int)m.wParam;
}
