/*
 * INT 33h - Microsoft Mouse driver (native BIOS implementation)
 * INT 74h - IRQ12 (PS/2 aux device) handler
 *
 * Никаких хуков в main.c: драйвер живёт целиком внутри BIOS и общается с
 * мышью так же, как настоящий DOS-драйвер — через контроллер 8042 (порты
 * 0x60/0x64) и прерывание IRQ12. Хостовые мыши (PS/2, USB, NES-эмуляция)
 * уже кладут пакеты в i8042 через ps2_mouse_event(), поэтому все три
 * источника работают автоматически.
 *
 * Активируется только при native BIOS: bios_33h_install() зовётся из
 * bios_post(). При внешнем BIOS (SeaBIOS) handlers[] не задействованы,
 * вектора не подменяются — поведение не меняется.
 *
 * Если гость загрузит свой драйвер мыши (CTMOUSE и т.п.), он перепишет
 * вектора INT 33h/INT 74h в IVT, и наш код просто перестанет вызываться.
 *
 * Курсор: программный; текстовые режимы и VGA mode 13h.
 * Обработчики событий AX=000Ch/0014h вызываются синхронно как FAR callback.
 */

#include <string.h>
#include "pico/time.h"
#include "286/cpu.h"
#include "bios.h"

/* Existing synchronous guest FAR-call helper from the FreeDOS runtime. */
extern void cpu_far_call(CPU *cpu, uint16_t seg, uint16_t off);

#define BDA_VIDEO_MODE   0x449u
#define BDA_VIDEO_COLS   0x44Au
#define BDA_VIDEO_ROWS   0x484u   /* rows - 1 */
#define BDA_VIDEO_PAGE   0x462u
#define BDA_VIDEO_PGSIZE 0x44Cu

#define MOUSE_BUTTONS    2

#define VIRT_W  640
#define VIRT_H  200

/* 8042 */
#define KBC_DATA         0x60
#define KBC_STATUS       0x64
#define KBC_CMD          0x64
#define KBC_ST_OBF       0x01
#define KBC_ST_IBF       0x02
#define KBC_ST_AUX_OBF   0x20

typedef struct {
    int      installed;

    int      x, y;
    int      minx, maxx;
    int      miny, maxy;

    uint8_t  buttons;

    uint16_t press_cnt[3], press_x[3], press_y[3];
    uint16_t rel_cnt[3],   rel_x[3],   rel_y[3];

    int      mickey_x, mickey_y;
    int      frac_x, frac_y;

    int      hide_count;          /* курсор виден только при == 0 */

    uint16_t scr_mask, cur_mask;

    int      mpp_x, mpp_y;        /* mickeys per 8 pixels */
    uint8_t  sens_x, sens_y;
    uint16_t sens_d;               /* double-speed threshold, mickeys/sec */
    uint64_t last_move_us;

    int      drawn;
    uint8_t  drawn_mode;
    uint32_t drawn_addr;
    uint16_t drawn_cell;

    int16_t  gfx_hot_x, gfx_hot_y;
    uint16_t gfx_screen[16], gfx_cursor[16];
    int16_t  gfx_saved_x, gfx_saved_y;
    uint8_t  gfx_saved_w, gfx_saved_h;
    uint8_t  gfx_saved[16 * 16];

    uint16_t cb_mask, cb_seg, cb_off;

    /* сборка PS/2-пакета в INT 74h */
    uint8_t  pkt[4];
    uint8_t  pkt_idx;
} MouseState;

static MouseState m;

/* ------------------------------------------------------------------ */
/* Текстовый курсор                                                    */
/* ------------------------------------------------------------------ */

static int text_mode_base(uint32_t *base, int *cols, int *rows)
{
    uint8_t mode = pload8(BDA_VIDEO_MODE);

    if (mode == 0x07)      *base = 0xB0000u;
    else if (mode <= 0x03) *base = 0xB8000u;
    else                   return 0;

    *cols = pload16(BDA_VIDEO_COLS);
    if (*cols <= 0 || *cols > 132) *cols = 80;
    *rows = pload8(BDA_VIDEO_ROWS) + 1;
    if (*rows <= 0 || *rows > 60) *rows = 25;

    uint16_t pgsize = pload16(BDA_VIDEO_PGSIZE);
    uint8_t  page   = pload8(BDA_VIDEO_PAGE);
    if (pgsize == 0) pgsize = 0x1000;
    *base += (uint32_t)page * pgsize;
    return 1;
}

static void cursor_erase(void)
{
    if (!m.drawn)
        return;

    if (m.drawn_mode == 0x13) {
        unsigned k = 0;
        for (int yy = 0; yy < m.gfx_saved_h; yy++) {
            uint32_t addr = 0xA0000u +
                            (uint32_t)(m.gfx_saved_y + yy) * 320u +
                            (uint32_t)m.gfx_saved_x;
            for (int xx = 0; xx < m.gfx_saved_w; xx++)
                pstore8(addr + (uint32_t)xx, m.gfx_saved[k++]);
        }
    } else {
        pstore16(m.drawn_addr, m.drawn_cell);
    }
    m.drawn = 0;
}

static void cursor_draw_mode13(void)
{
    /* Microsoft mouse coordinates are normally 0..639 in 320-pixel modes.
       If the guest explicitly selected a <=319 range, honor that literally. */
    int px = (m.maxx > 319) ? (m.x >> 1) : m.x;
    int py = m.y;
    int left = px - m.gfx_hot_x;
    int top  = py - m.gfx_hot_y;
    int x0 = left < 0 ? 0 : left;
    int y0 = top  < 0 ? 0 : top;
    int x1 = left + 16;
    int y1 = top  + 16;
    if (x1 > 320) x1 = 320;
    if (y1 > 200) y1 = 200;
    if (x0 >= x1 || y0 >= y1)
        return;

    m.gfx_saved_x = (int16_t)x0;
    m.gfx_saved_y = (int16_t)y0;
    m.gfx_saved_w = (uint8_t)(x1 - x0);
    m.gfx_saved_h = (uint8_t)(y1 - y0);

    unsigned k = 0;
    for (int y = y0; y < y1; y++) {
        int cy = y - top;
        uint16_t sm = m.gfx_screen[cy];
        uint16_t cm = m.gfx_cursor[cy];
        uint32_t addr = 0xA0000u + (uint32_t)y * 320u + (uint32_t)x0;

        for (int x = x0; x < x1; x++) {
            int cx = x - left;
            uint16_t bit = (uint16_t)(0x8000u >> cx);
            uint8_t old = pload8(addr);
            uint8_t out = (sm & bit) ? old : 0;
            if (cm & bit)
                out ^= 0xFF;
            m.gfx_saved[k++] = old;
            pstore8(addr++, out);
        }
    }

    m.drawn_mode = 0x13;
    m.drawn = 1;
}

static void cursor_draw(void)
{
    uint32_t base;
    int cols, rows;

    if (m.drawn || m.hide_count != 0)
        return;

    if (pload8(BDA_VIDEO_MODE) == 0x13) {
        cursor_draw_mode13();
        return;
    }

    if (!text_mode_base(&base, &cols, &rows))
        return;

    int cx = m.x >> 3;
    int cy = m.y >> 3;
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    if (cx >= cols) cx = cols - 1;
    if (cy >= rows) cy = rows - 1;

    uint32_t addr = base + (uint32_t)(cy * cols + cx) * 2u;
    uint16_t cell = pload16(addr);

    m.drawn_addr = addr;
    m.drawn_cell = cell;
    m.drawn_mode = pload8(BDA_VIDEO_MODE);
    m.drawn      = 1;

    pstore16(addr, (uint16_t)((cell & m.scr_mask) ^ m.cur_mask));
}

static void cursor_refresh(void)
{
    cursor_erase();
    cursor_draw();
}

/* ------------------------------------------------------------------ */
/* User event callback (INT 33h AX=000Ch/0014h)                        */
/* ------------------------------------------------------------------ */
static void mouse_callback(CPU *cpu, uint16_t events, int dx, int dy)
{
    if (!(events & m.cb_mask) || (m.cb_seg == 0 && m.cb_off == 0))
        return;

    uint16_t ax = CPU_AX, bx = CPU_BX, cx = CPU_CX, dxr = CPU_DX;
    uint16_t si = CPU_SI, di = CPU_DI, bp = CPU_BP;
    uint16_t ds = CPU_DS, es = CPU_ES, ss = CPU_SS, sp = CPU_SP;
    uint16_t flags = cpu_getflags(cpu);

    CPU_AX = (uint16_t)(events & m.cb_mask);
    CPU_BX = m.buttons;
    CPU_CX = (uint16_t)m.x;
    CPU_DX = (uint16_t)m.y;
    CPU_SI = (uint16_t)(int16_t)dx;
    CPU_DI = (uint16_t)(int16_t)dy;

    cpu_far_call(cpu, m.cb_seg, m.cb_off);

    SET_SS(ss); CPU_SP = sp;
    SET_DS(ds); SET_ES(es);
    CPU_AX = ax; CPU_BX = bx; CPU_CX = cx; CPU_DX = dxr;
    CPU_SI = si; CPU_DI = di; CPU_BP = bp;
    cpu_setflags(cpu, flags, (uword)~flags);
}

static int mouse_sensitivity_q12(uint8_t value)
{
    if (value < 1) value = 1;
    if (value > 100) value = 100;
    if (value == 50) return 4096;
    int n = (int)value - 1;
    return 1365 + (n * n * 4096 + 1800) / 3600;
}

/* ------------------------------------------------------------------ */
/* Применение движения                                                 */
/* dx/dy — экранные (+x вправо, +y вниз)                               */
/* ------------------------------------------------------------------ */
static void mouse_apply(CPU *cpu, int dx, int dy, uint8_t nb)
{
    m.mickey_x += dx;
    m.mickey_y += dy;

    int move_x = dx, move_y = dy;
    if (dx || dy) {
        uint64_t now = time_us_64();
        if (m.last_move_us) {
            uint64_t dt = now - m.last_move_us;
            uint32_t distance = (uint32_t)(dx < 0 ? -dx : dx);
            uint32_t ay = (uint32_t)(dy < 0 ? -dy : dy);
            if (ay > distance) distance = ay;
            uint32_t threshold = m.sens_d ? m.sens_d : 64;
            if (dt && (uint64_t)distance * 1000000ull > (uint64_t)threshold * dt) {
                move_x <<= 1;
                move_y <<= 1;
            }
        }
        m.last_move_us = now;
    }

    int sx = mouse_sensitivity_q12(m.sens_x);
    int sy = mouse_sensitivity_q12(m.sens_y);
    int mx = (m.mpp_x ? m.mpp_x : 8) * 4096;
    int my = (m.mpp_y ? m.mpp_y : 16) * 4096;
    m.frac_x += move_x * 8 * sx;
    m.frac_y += move_y * 8 * sy;

    int px = m.frac_x / mx;
    int py = m.frac_y / my;
    m.frac_x -= px * mx;
    m.frac_y -= py * my;

    if (px || py) {
        m.x += px;
        m.y += py;
        if (m.x < m.minx) m.x = m.minx;
        if (m.x > m.maxx) m.x = m.maxx;
        if (m.y < m.miny) m.y = m.miny;
        if (m.y > m.maxy) m.y = m.maxy;
        cursor_refresh();
    }

    nb &= 7;
    uint8_t old = m.buttons;
    uint16_t events = (dx || dy) ? 0x0001u : 0;
    static const uint8_t press_event[3] = { 0x02, 0x08, 0x20 };
    static const uint8_t rel_event[3]   = { 0x04, 0x10, 0x40 };

    for (int b = 0; b < 3; b++) {
        uint8_t bit = (uint8_t)(1u << b);
        if ((nb & bit) && !(old & bit)) {
            m.press_cnt[b]++;
            m.press_x[b] = (uint16_t)m.x;
            m.press_y[b] = (uint16_t)m.y;
            events |= press_event[b];
        } else if (!(nb & bit) && (old & bit)) {
            m.rel_cnt[b]++;
            m.rel_x[b] = (uint16_t)m.x;
            m.rel_y[b] = (uint16_t)m.y;
            events |= rel_event[b];
        }
    }
    m.buttons = nb;
    mouse_callback(cpu, events, dx, dy);
}

/* ------------------------------------------------------------------ */
/* INT 74h — IRQ12                                                     */
/* ------------------------------------------------------------------ */
bool bios_74h(CPU* cpu)
{
    uint8_t st = cpu_portin8(KBC_STATUS);

    if ((st & (KBC_ST_OBF | KBC_ST_AUX_OBF)) == (KBC_ST_OBF | KBC_ST_AUX_OBF)) {
        uint8_t b = cpu_portin8(KBC_DATA);

        /* ACK/RESEND/self-test bytes are not packet data. */
        if (m.pkt_idx == 0 && (b == 0xFA || b == 0xFE || b == 0xAA)) {
            /* ignore asynchronous mouse command response */
        }
        /* ресинхронизация: бит3 первого байта пакета всегда 1 */
        else if (m.pkt_idx == 0 && !(b & 0x08)) {
            /* мусор — игнорируем */
        } else {
            m.pkt[m.pkt_idx++] = b;

            if (m.pkt_idx >= 3) {
                m.pkt_idx = 0;

                uint8_t f = m.pkt[0];
                int dx = m.pkt[1];
                int dy = m.pkt[2];
                if (f & 0x10) dx |= ~0xFF;      /* знак X */
                if (f & 0x20) dy |= ~0xFF;      /* знак Y */
                if (f & 0xC0) { dx = 0; dy = 0; }   /* overflow */

                /* PS/2: +Y = вверх; экран: +Y = вниз */
                if (m.installed)
                    mouse_apply(cpu, dx, -dy, (uint8_t)(f & 0x07));
            }
        }
    }

    /* EOI: сначала slave, потом master */
    cpu_portout8(0xA0, 0x20);
    cpu_portout8(0x20, 0x20);
    return true;
}

/* ------------------------------------------------------------------ */
/* Инициализация 8042 + мыши (как это делает настоящий драйвер)         */
/* ------------------------------------------------------------------ */
static void kbc_wait_ibe(CPU* cpu)
{
    for (int i = 0; i < 10000; i++)
        if (!(cpu_portin8(KBC_STATUS) & KBC_ST_IBF))
            return;
}

static int kbc_wait_aux_obf(CPU* cpu)
{
    for (int i = 0; i < 10000; i++) {
        uint8_t st = cpu_portin8(KBC_STATUS);
        if ((st & (KBC_ST_OBF | KBC_ST_AUX_OBF)) ==
            (KBC_ST_OBF | KBC_ST_AUX_OBF))
            return 1;

        /*
         * Do not consume a pending keyboard byte while waiting for a mouse
         * reply.  Leave it queued for IRQ1 and let IRQ12 discard any delayed
         * mouse ACK asynchronously if necessary.
         */
        if ((st & KBC_ST_OBF) && !(st & KBC_ST_AUX_OBF))
            return 0;
    }
    return 0;
}

static void kbc_cmd(CPU* cpu, uint8_t c)
{
    kbc_wait_ibe(cpu);
    cpu_portout8(KBC_CMD, c);
}

static void kbc_data(CPU* cpu, uint8_t d)
{
    kbc_wait_ibe(cpu);
    cpu_portout8(KBC_DATA, d);
}

/* послать байт мыши и съесть ACK, только если это действительно AUX */
static void aux_send(CPU* cpu, uint8_t d)
{
    kbc_cmd(cpu, 0xD4);          /* следующий байт — в aux-порт */
    kbc_data(cpu, d);
    if (kbc_wait_aux_obf(cpu))
        (void)cpu_portin8(KBC_DATA);   /* mouse ACK (normally 0xFA) */
}

static void mouse_hw_init(CPU* cpu)
{
    /* включить aux-порт */
    kbc_cmd(cpu, 0xA8);

    /* command byte: разрешить IRQ12 (bit1) и снять disable-aux (bit5) */
    kbc_cmd(cpu, 0x20);
    /*
     * 0x20 returns the controller command byte through the controller output
     * buffer, not through the AUX device, so wait for ordinary OBF here.
     */
    uint8_t cb = 0x45;
    for (int i = 0; i < 10000; i++) {
        if (cpu_portin8(KBC_STATUS) & KBC_ST_OBF) {
            cb = cpu_portin8(KBC_DATA);
            break;
        }
    }
    cb |=  0x02;    /* enable aux (IRQ12) interrupt */
    cb &= ~0x20;    /* aux clock enable */
    cb |=  0x01;    /* keep keyboard IRQ1 on */
    kbc_cmd(cpu, 0x60);
    kbc_data(cpu, cb);

    aux_send(cpu, 0xF6);   /* set defaults */
    aux_send(cpu, 0xF4);   /* enable data reporting (stream mode) */

    m.pkt_idx = 0;
}

/* ------------------------------------------------------------------ */
void bios_33h_reset(void)
{
    int was = m.installed;
    memset(&m, 0, sizeof(m));
    m.installed = was;

    m.minx = 0; m.maxx = VIRT_W - 1;
    m.miny = 0; m.maxy = VIRT_H - 1;
    m.x = VIRT_W / 2;
    m.y = VIRT_H / 2;

    m.scr_mask = 0x77FF;
    m.cur_mask = 0x7700;

    /* Default Microsoft-style 16x16 arrow. */
    static const uint16_t def_screen[16] = {
        0x3FFF,0x1FFF,0x0FFF,0x07FF,0x03FF,0x01FF,0x00FF,0x007F,
        0x003F,0x001F,0x01FF,0x10FF,0x30FF,0xF87F,0xF87F,0xFC7F
    };
    static const uint16_t def_cursor[16] = {
        0x0000,0x4000,0x6000,0x7000,0x7800,0x7C00,0x7E00,0x7F00,
        0x7F80,0x7C00,0x6C00,0x4600,0x0600,0x0300,0x0300,0x0000
    };
    memcpy(m.gfx_screen, def_screen, sizeof(def_screen));
    memcpy(m.gfx_cursor, def_cursor, sizeof(def_cursor));
    m.gfx_hot_x = 0;
    m.gfx_hot_y = 0;

    m.mpp_x = 8;
    m.mpp_y = 16;
    m.sens_x = m.sens_y = 50;
    m.sens_d = 64;
    m.last_move_us = 0;

    m.hide_count = -1;      /* скрыт */
    m.drawn = 0;
    m.pkt_idx = 0;
}

/* Зовётся из bios_post(). enabled == pc->mouse_enabled */
void bios_33h_install(CPU* cpu, int enabled)
{
    bios_33h_reset();
    m.installed = enabled ? 1 : 0;
    if (enabled)
        mouse_hw_init(cpu);
}

/* ------------------------------------------------------------------ */
/* INT 33h                                                             */
/* ------------------------------------------------------------------ */
bool bios_33h(CPU* cpu)
{
    /*
     * A disabled mouse driver still has a callable INT 33h entry point.
     * The standard reset/status probe reports AX=0000h when no driver is
     * installed.  Other functions are harmless no-ops in that state.
     */
    if (!m.installed) {
        if (CPU_AX == 0x0000 || CPU_AX == 0x0021) {
            CPU_AX = 0x0000;
            CPU_BX = 0x0000;
        }
        return true;
    }
    
    switch (CPU_AX) {

    case 0x0000:    /* Reset driver and read status */
    case 0x0021:    /* Software reset */
        cursor_erase();
        bios_33h_reset();
        mouse_hw_init(cpu);
        CPU_AX = 0xFFFF;
        CPU_BX = MOUSE_BUTTONS;
        break;

    case 0x0001:    /* Show cursor */
        m.hide_count++;
        if (m.hide_count > 0)
            m.hide_count = 0;
        if (m.hide_count == 0)
            cursor_draw();
        break;

    case 0x0002:    /* Hide cursor */
        if (m.hide_count == 0)
            cursor_erase();
        m.hide_count--;
        break;

    case 0x0003:    /* Get position and button status */
        CPU_BX = m.buttons;
        CPU_CX = (uint16_t)m.x;
        CPU_DX = (uint16_t)m.y;
        break;

    case 0x0004:    /* Set position */
        m.x = CPU_CX;
        m.y = CPU_DX;
        if (m.x < m.minx) m.x = m.minx;
        if (m.x > m.maxx) m.x = m.maxx;
        if (m.y < m.miny) m.y = m.miny;
        if (m.y > m.maxy) m.y = m.maxy;
        cursor_refresh();
        break;

    case 0x0005: {  /* Get button press data */
        int b = CPU_BX & 3;
        if (b > 2) b = 2;
        CPU_AX = m.buttons;
        CPU_BX = m.press_cnt[b];
        CPU_CX = m.press_x[b];
        CPU_DX = m.press_y[b];
        m.press_cnt[b] = 0;
        break;
    }

    case 0x0006: {  /* Get button release data */
        int b = CPU_BX & 3;
        if (b > 2) b = 2;
        CPU_AX = m.buttons;
        CPU_BX = m.rel_cnt[b];
        CPU_CX = m.rel_x[b];
        CPU_DX = m.rel_y[b];
        m.rel_cnt[b] = 0;
        break;
    }

    case 0x0007: {  /* Set horizontal min/max */
        int a = (int16_t)CPU_CX, b = (int16_t)CPU_DX;
        if (a > b) { int t = a; a = b; b = t; }
        m.minx = a; m.maxx = b;
        if (m.x < m.minx) m.x = m.minx;
        if (m.x > m.maxx) m.x = m.maxx;
        cursor_refresh();
        break;
    }

    case 0x0008: {  /* Set vertical min/max */
        int a = (int16_t)CPU_CX, b = (int16_t)CPU_DX;
        if (a > b) { int t = a; a = b; b = t; }
        m.miny = a; m.maxy = b;
        if (m.y < m.miny) m.y = m.miny;
        if (m.y > m.maxy) m.y = m.maxy;
        cursor_refresh();
        break;
    }

    case 0x0009: {  /* Define graphics cursor */
        cursor_erase();
        m.gfx_hot_x = (int16_t)CPU_BX;
        m.gfx_hot_y = (int16_t)CPU_CX;
        uint32_t p = ((uint32_t)CPU_ES << 4) + CPU_DX;
        for (int i = 0; i < 16; i++)
            m.gfx_screen[i] = pload16(p + (uint32_t)i * 2u);
        p += 32;
        for (int i = 0; i < 16; i++)
            m.gfx_cursor[i] = pload16(p + (uint32_t)i * 2u);
        cursor_draw();
        break;
    }

    case 0x000A:    /* Define text cursor */
        cursor_erase();
        if (CPU_BX == 0) {
            m.scr_mask = CPU_CX;
            m.cur_mask = CPU_DX;
        } else {
            m.scr_mask = 0x77FF;
            m.cur_mask = 0x7700;
        }
        cursor_draw();
        break;

    case 0x000B:    /* Read motion counters */
        CPU_CX = (uint16_t)(int16_t)m.mickey_x;
        CPU_DX = (uint16_t)(int16_t)m.mickey_y;
        m.mickey_x = 0;
        m.mickey_y = 0;
        break;

    case 0x000C:    /* Define event handler */
        m.cb_mask = CPU_CX;
        m.cb_seg  = CPU_ES;
        m.cb_off  = CPU_DX;
        break;

    case 0x000D:
    case 0x000E:
        break;

    case 0x000F:    /* Set mickeys per 8 pixels */
        m.mpp_x = CPU_CX ? (int16_t)CPU_CX : 8;
        m.mpp_y = CPU_DX ? (int16_t)CPU_DX : 16;
        break;

    case 0x0010:    /* Define exclusion area — игнорируем */
        break;

    case 0x0013:    /* Set double-speed threshold, mickeys/sec */
        m.sens_d = CPU_DX ? CPU_DX : 64;
        m.last_move_us = 0;
        break;

    case 0x0014: {  /* Exchange event handler */
        uint16_t om = m.cb_mask, os = m.cb_seg, oo = m.cb_off;
        m.cb_mask = CPU_CX;
        m.cb_seg  = CPU_ES;
        m.cb_off  = CPU_DX;
        CPU_CX = om;
        SET_ES(os);
        CPU_DX = oo;
        break;
    }

    case 0x0015:    /* Get driver state storage size */
        CPU_BX = sizeof(MouseState);
        break;

    case 0x0016: {  /* Save driver state -> ES:DX */
        uint32_t p = ((uint32_t)CPU_ES << 4) + CPU_DX;
        const uint8_t *s = (const uint8_t *)&m;
        for (unsigned i = 0; i < sizeof(MouseState); i++)
            pstore8(p + i, s[i]);
        break;
    }

    case 0x0017: {  /* Restore driver state <- ES:DX */
        uint32_t p = ((uint32_t)CPU_ES << 4) + CPU_DX;
        uint8_t *s = (uint8_t *)&m;
        cursor_erase();
        for (unsigned i = 0; i < sizeof(MouseState); i++)
            s[i] = pload8(p + i);
        m.drawn = 0;
        cursor_draw();
        break;
    }

    case 0x001A: {  /* Set mouse sensitivity */
        uint16_t sx = CPU_BX, sy = CPU_CX;
        if (sx < 1) sx = 1; else if (sx > 100) sx = 100;
        if (sy < 1) sy = 1; else if (sy > 100) sy = 100;
        m.sens_x = (uint8_t)sx;
        m.sens_y = (uint8_t)sy;
        m.sens_d = CPU_DX ? CPU_DX : 64;
        m.last_move_us = 0;
        break;
    }

    case 0x001B:    /* Get mouse sensitivity */
        CPU_BX = m.sens_x;
        CPU_CX = m.sens_y;
        CPU_DX = m.sens_d;
        break;

    case 0x001C:
    case 0x001D:
        break;

    case 0x001E:    /* Get CRT page */
        CPU_BX = pload8(BDA_VIDEO_PAGE);
        break;

    case 0x001F:    /* Disable driver */
        cursor_erase();
        CPU_AX = 0x001F;
        SET_ES(0);
        CPU_BX = 0;
        break;

    case 0x0020:    /* Enable driver */
        break;

    case 0x0024:    /* Get version / mouse type / IRQ  <<< это читает SysInfo */
        CPU_BX = 0x0800;    /* версия 8.00 (BH=major, BL=minor) */
        CPU_CH = 0x04;      /* 04h = PS/2 mouse */
        CPU_CL = 0x00;      /* PS/2: IRQ не сообщается */
        break;

    case 0x0026:    /* Get maximum virtual coordinates */
        CPU_BX = m.installed ? 0x0000 : 0xFFFF;
        CPU_CX = (uint16_t)m.maxx;
        CPU_DX = (uint16_t)m.maxy;
        break;

    case 0x0027:    /* Get screen/cursor masks and mickey counts */
        CPU_AX = m.scr_mask;
        CPU_BX = m.cur_mask;
        CPU_CX = (uint16_t)(int16_t)m.mickey_x;
        CPU_DX = (uint16_t)(int16_t)m.mickey_y;
        break;

    default:
        break;
    }

    return true;
}
