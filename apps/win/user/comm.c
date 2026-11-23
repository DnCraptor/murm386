/*
 * comm.c - USER's communications functions: the serial ports (COM1-COM4,
 * 8250 UARTs at their standard addresses, polled: the emulator's UART
 * hands it a byte at a time and holds the next until it is read, so
 * nothing is lost between polls) and the parallel ones (LPT1-LPT3, DOS's
 * devices, written through).
 *
 * OpenComm gives an id (COMn: n-1; LPTn: 80h + n-1) with queues of the
 * sizes asked; the ports are polled as input is (user_poll_input): what
 * came in goes to the input queue, the output queue to the port as it
 * takes it (XON/XOFF, CTS and DSR flow as the DCB asks). The events
 * (SetCommEventMask's word) and WM_COMMNOTIFY (EnableCommNotification)
 * as Windows 3.1's COMM.DRV has them.
 *
 * The host's tests have COM1 alone, its line looped back (what is sent
 * comes in).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "wincore.h"
#include "user.h"

#define COMS 4
#define LPTS 3
#define MAXQ 0x8000u

#if defined(__arm__) || defined(__thumb__)
#include "conio.h"
static uint8_t port_in(uint16_t port) { return (uint8_t)inp(port); }
static void port_out(uint16_t port, uint8_t v) { outp(port, v); }
#else
/* the host's COM1: an 8250 whose output comes back in (a byte at a time,
 * as the emulator's does) */
static struct { uint8_t lcr, mcr, ier, dll, dlh, rbr, ready; uint8_t q[64]; int h, n; } loop;

static uint8_t port_in(uint16_t port)
{
    if ((port & ~7u) != 0x3f8)
        return 0xff;
    if (!loop.ready && loop.n) {
        loop.rbr = loop.q[loop.h];
        loop.h = (loop.h + 1) % 64;
        loop.n--;
        loop.ready = 1;
    }
    switch (port & 7) {
    case 0:
        if (loop.lcr & 0x80)
            return loop.dll;
        loop.ready = 0;
        return loop.rbr;
    case 1: return loop.lcr & 0x80 ? loop.dlh : loop.ier;
    case 2: return 1;
    case 3: return loop.lcr;
    case 4: return loop.mcr;
    case 5: return (uint8_t)(0x60 | loop.ready);
    case 6: return 0xb0;                /* CTS, DSR, DCD */
    }
    return 0;
}

static void port_out(uint16_t port, uint8_t v)
{
    if ((port & ~7u) != 0x3f8)
        return;
    switch (port & 7) {
    case 0:
        if (loop.lcr & 0x80)
            loop.dll = v;
        else if (loop.n < 64) {
            loop.q[(loop.h + loop.n) % 64] = v;
            loop.n++;
        }
        break;
    case 1:
        if (loop.lcr & 0x80)
            loop.dlh = v;
        else
            loop.ier = v;
        break;
    case 3: loop.lcr = v; break;
    case 4: loop.mcr = v; break;
    }
}
#endif

/* the UART's registers */
enum { RBR = 0, THR = 0, IER = 1, IIR = 2, LCR = 3, MCR = 4, LSR = 5, MSR = 6 };

typedef struct {
    int open;
    HTASK task;
    uint16_t base;                      /* the UART's (COM) */
    HFILE dev;                          /* DOS's device (LPT) */
    DCB dcb;
    uint8_t *in, *out;
    UINT in_size, out_size, in_head, in_count, out_head, out_count;
    UINT error;                         /* CE_*, GetCommError's */
    UINT events, mask;                  /* EV_*: what happened, what is noted */
    int unget;                          /* UngetCommChar's, -1 none */
    int tx_held;                        /* XOFF received */
    int xoff_sent;
    int brk;
    uint8_t msr;                        /* the modem's lines as last seen */
    HWND notify;
    int rx_notify, tx_notify;           /* EnableCommNotification's thresholds */
    int rx_told, tx_told;               /* CN_RECEIVE, CN_TRANSMIT posted, not yet cleared */
} wcomm;

static wcomm coms[COMS], lpts[LPTS];
static const uint16_t com_base[COMS] = { 0x3f8, 0x2f8, 0x3e8, 0x2e8 };
static void (*event_hook)(int cid, UINT events);

void user_comm_event_hook(void (*hook)(int cid, UINT events)) { event_hook = hook; }

static wcomm *comm_of(int cid)
{
    wcomm *c = cid >= 0 && cid < COMS ? &coms[cid] :
               (cid & 0x80) && (cid & 0x7f) < LPTS ? &lpts[cid & 0x7f] : NULL;
    return c && c->open ? c : NULL;
}

static int cid_of(const wcomm *c)
{
    return c >= lpts && c < lpts + LPTS ? 0x80 | (int)(c - lpts) : (int)(c - coms);
}

/* the events noted: those new */
static UINT set_events(wcomm *c, UINT ev)
{
    UINT was = c->events;
    c->events |= ev & c->mask;
    if (c->events != was && event_hook)
        event_hook(cid_of(c), c->events);
    return c->events & ~was;
}

/* "COM1", "com2:", "LPT1": its id, -1 if none such */
static int device_id(LPCSTR name)
{
    if (!name)
        return -1;
    char n[8];
    int k = 0;
    while (name[k] && name[k] != ':' && name[k] != ' ' && k < 7) {
        n[k] = name[k];
        k++;
    }
    n[k] = 0;
    if (k == 4 && n[3] >= '1' && n[3] <= '9') {
        int i = n[3] - '1';
        n[3] = 0;
        if (!lstrcmpi(n, "COM") && i < COMS)
            return i;
        if (!lstrcmpi(n, "LPT") && i < LPTS)
            return 0x80 | i;
    }
    if (!lstrcmpi(n, "AUX"))
        return 0;
    if (!lstrcmpi(n, "PRN"))
        return 0x80;
    return -1;
}

/* the UART there: its line control register keeps what it is given */
static int uart_there(uint16_t base)
{
    uint8_t was = port_in((uint16_t)(base + LCR));
    port_out((uint16_t)(base + LCR), 0x1b);
    int there = port_in((uint16_t)(base + LCR)) == 0x1b;
    port_out((uint16_t)(base + LCR), was);
    return there;
}

static const struct { UINT cbr; DWORD baud; } cbrs[] = {
    { CBR_110, 110 }, { CBR_300, 300 }, { CBR_600, 600 }, { CBR_1200, 1200 },
    { CBR_2400, 2400 }, { CBR_4800, 4800 }, { CBR_9600, 9600 }, { CBR_14400, 14400 },
    { CBR_19200, 19200 }, { CBR_38400, 38400 }, { CBR_56000, 56000 },
    { CBR_128000, 128000 }, { CBR_256000, 256000 },
};

static DWORD baud_of(UINT rate)
{
    for (unsigned i = 0; i < sizeof(cbrs) / sizeof(cbrs[0]); i++)
        if (cbrs[i].cbr == rate)
            return cbrs[i].baud;
    return rate;
}

/* the DCB to the UART: the speed, the frame, DTR and RTS */
static int program(wcomm *c, const DCB *d)
{
    DWORD baud = baud_of(d->BaudRate);
    if (!baud || baud > 115200u)
        return IE_BAUDRATE;
    if (d->ByteSize < 5 || d->ByteSize > 8)
        return IE_BYTESIZE;
    if (d->Parity > SPACEPARITY || d->StopBits > TWOSTOPBITS)
        return IE_DEFAULT;
    if (!c->base)
        return 0;
    UINT div = (UINT)(115200u / baud);
    static const uint8_t parity[5] = { 0x00, 0x08, 0x18, 0x28, 0x38 };
    uint8_t lcr = (uint8_t)((d->ByteSize - 5) | (d->StopBits ? 0x04 : 0) | parity[d->Parity] |
                            (c->brk ? 0x40 : 0));
    port_out((uint16_t)(c->base + LCR), 0x80);
    port_out((uint16_t)(c->base + RBR), (uint8_t)div);
    port_out((uint16_t)(c->base + IER), (uint8_t)(div >> 8));
    port_out((uint16_t)(c->base + LCR), lcr);
    port_out((uint16_t)(c->base + IER), 0);  /* polled */
    port_out((uint16_t)(c->base + MCR), (uint8_t)((d->fDtrDisable ? 0 : 1) |
                                                  (d->fRtsDisable ? 0 : 2)));
    return 0;
}

int WINAPI OpenComm(LPCSTR name, UINT in_size, UINT out_size)
{
    int id = device_id(name);
    if (id < 0)
        return IE_BADID;
    wcomm *c = id & 0x80 ? &lpts[id & 0x7f] : &coms[id];
    if (c->open)
        return IE_OPEN;
    if (!in_size && !out_size)          /* only asked whether it is open */
        return IE_NOPEN;
    memset(c, 0, sizeof(*c));
    c->unget = -1;
    c->dev = HFILE_ERROR;
    if (id & 0x80) {
        char dev[8];
        lstrcpyn(dev, "LPT1", sizeof(dev));
        dev[3] = (char)('1' + (id & 0x7f));
        c->dev = _lopen(dev, OF_WRITE);
        if (c->dev == HFILE_ERROR)
            c->dev = _lcreat(dev, 0);
        if (c->dev == HFILE_ERROR)
            return IE_HARDWARE;
    } else {
        c->base = com_base[id];
        if (!uart_there(c->base))
            return IE_HARDWARE;
    }
    c->in_size = in_size < 16 ? 16 : in_size > MAXQ ? MAXQ : in_size;
    c->out_size = out_size < 16 ? 16 : out_size > MAXQ ? MAXQ : out_size;
    c->in = (uint8_t *)win_alloc(c->in_size);
    c->out = (uint8_t *)win_alloc(c->out_size);
    if (!c->in || !c->out) {
        win_free(c->in);
        win_free(c->out);
        if (c->dev != HFILE_ERROR)
            _lclose(c->dev);
        return IE_MEMORY;
    }
    /* WIN.INI's [ports] "COM1:=9600,n,8,1", else 9600 8N1 */
    char key[8], def[32];
    lstrcpyn(key, id & 0x80 ? "LPT1:" : "COM1:", sizeof(key));
    key[3] = (char)('1' + (id & 0x7f));
    GetProfileString("ports", key, "9600,n,8,1", def, sizeof(def));
    char spec[48];
    wsprintf(spec, "%.5s%.40s", key, def);
    if (BuildCommDCB(spec, &c->dcb))
        BuildCommDCB("COM1:9600,n,8,1", &c->dcb);
    c->dcb.Id = (BYTE)id;
    c->dcb.XonChar = 0x11;
    c->dcb.XoffChar = 0x13;
    c->dcb.XonLim = (UINT)(c->in_size / 4);
    c->dcb.XoffLim = (UINT)(c->in_size / 4);
    c->rx_notify = c->tx_notify = -1;
    c->task = GetCurrentTask();
    c->open = 1;
    program(c, &c->dcb);
    if (c->base)
        c->msr = port_in((uint16_t)(c->base + MSR));
    return id;
}

int WINAPI CloseComm(int cid)
{
    wcomm *c = comm_of(cid);
    if (!c)
        return -1;
    user_comm_poll();                   /* what it has sent, out */
    if (c->base)
        port_out((uint16_t)(c->base + MCR), 0);
    if (c->dev != HFILE_ERROR)
        _lclose(c->dev);
    win_free(c->in);
    win_free(c->out);
    memset(c, 0, sizeof(*c));
    return 0;
}

/* "COM1:9600,n,8,1" (MODE's: "COM1:96,N,8,1,P" too; "baud=9600 parity=N
 * data=8 stop=1" as well) */
int WINAPI BuildCommDCB(LPCSTR def, DCB *d)
{
    int id = device_id(def);
    if (id < 0 || !d)
        return IE_BADID;
    const char *p = def;
    while (*p && *p != ':' && *p != ' ')
        p++;
    if (*p)
        p++;
    DWORD f[4] = { 0, 'n', 8, 1 };
    int stop15 = 0, retry = 0;
    for (int k = 0; k < 5 && *p; k++) {
        while (*p == ' ' || *p == '\t')
            p++;
        const char *q = p;
        while (*q && *q != ',')
            q++;
        if (k == 0) {
            DWORD v = 0;
            for (const char *s = p; s < q && *s >= '0' && *s <= '9'; s++)
                v = v * 10 + (DWORD)(*s - '0');
            /* MODE's two digits: 11 110, 30 300, 60 600, 12 1200, 24 2400 ... */
            if (v == 11 || v == 15 || v == 30 || v == 60 || v == 12 || v == 24 || v == 48 ||
                v == 96 || v == 19)
                v = v == 11 ? 110 : v == 15 ? 150 : v == 19 ? 19200 : v * 100;
            f[0] = v;
        } else if (k == 1 && q > p) {
            f[1] = (DWORD)(*p | 0x20);
        } else if (k == 2 && q > p) {
            f[2] = (DWORD)(*p - '0');
        } else if (k == 3 && q > p) {
            f[3] = (DWORD)(*p - '0');
            stop15 = q - p >= 3 && p[1] == '.' && p[2] == '5';
        } else if (k == 4 && q > p) {
            retry = (*p | 0x20) == 'p';
        }
        p = *q ? q + 1 : q;
    }
    (void)retry;
    if (!f[0] || f[0] > 115200u)
        return IE_BAUDRATE;
    if (f[2] < 5 || f[2] > 8)
        return IE_BYTESIZE;
    BYTE parity = f[1] == 'n' ? NOPARITY : f[1] == 'o' ? ODDPARITY : f[1] == 'e' ? EVENPARITY :
                  f[1] == 'm' ? MARKPARITY : f[1] == 's' ? SPACEPARITY : 0xff;
    if (parity == 0xff || (f[3] != 1 && f[3] != 2))
        return IE_DEFAULT;
    memset(d, 0, sizeof(*d));
    d->Id = (BYTE)id;
    d->BaudRate = f[0] > 0xfeffu ? CBR_128000 : (UINT)f[0];
    for (unsigned i = 0; i < sizeof(cbrs) / sizeof(cbrs[0]); i++)
        if (cbrs[i].baud == f[0] && f[0] > 0xfeffu)
            d->BaudRate = cbrs[i].cbr;
    d->ByteSize = (BYTE)f[2];
    d->Parity = parity;
    d->StopBits = stop15 ? ONE5STOPBITS : f[3] == 2 ? TWOSTOPBITS : ONESTOPBIT;
    d->fBinary = 1;
    d->fParity = parity != NOPARITY;
    d->XonChar = 0x11;
    d->XoffChar = 0x13;
    d->XonLim = 10;
    d->XoffLim = 10;
    return 0;
}

int WINAPI SetCommState(const DCB *d)
{
    wcomm *c = d ? comm_of(d->Id) : NULL;
    if (!c)
        return IE_BADID;
    int r = program(c, d);
    if (r)
        return r;
    c->dcb = *d;
    return 0;
}

int WINAPI GetCommState(int cid, DCB *d)
{
    wcomm *c = comm_of(cid);
    if (!c || !d)
        return IE_BADID;
    *d = c->dcb;
    return 0;
}

/* the modem's lines: CTS 10h, DSR 20h, RI 40h, DCD 80h */
static uint8_t modem(wcomm *c)
{
    return c->base ? port_in((uint16_t)(c->base + MSR)) : 0xb0;
}

static int may_send(wcomm *c)
{
    if (c->tx_held || c->brk)
        return 0;
    uint8_t m = (c->dcb.fOutxCtsFlow || c->dcb.fOutxDsrFlow) ? modem(c) : 0;
    return (!c->dcb.fOutxCtsFlow || (m & 0x10)) && (!c->dcb.fOutxDsrFlow || (m & 0x20));
}

/* a byte out now, if the transmitter takes it */
static int send_now(wcomm *c, uint8_t b)
{
    if (!(port_in((uint16_t)(c->base + LSR)) & 0x20))
        return 0;
    port_out((uint16_t)(c->base + THR), b);
    return 1;
}

static void notify(wcomm *c, UINT fresh)
{
    if (!c->notify || !IsWindow(c->notify))
        return;
    UINT n = 0;
    if (c->rx_notify >= 0 && !c->rx_told && c->in_count && c->in_count >= (UINT)c->rx_notify) {
        c->rx_told = 1;
        n |= CN_RECEIVE;
    }
    if (c->in_count < (UINT)(c->rx_notify > 0 ? c->rx_notify : 1))
        c->rx_told = 0;
    if (c->tx_notify >= 0 && !c->tx_told && c->out_count < (UINT)c->tx_notify) {
        c->tx_told = 1;
        n |= CN_TRANSMIT;
    }
    if (c->out_count >= (UINT)(c->tx_notify > 0 ? c->tx_notify : 1))
        c->tx_told = 0;
    if (fresh)
        n |= CN_EVENT;
    if (n)
        PostMessage(c->notify, WM_COMMNOTIFY, (WPARAM)cid_of(c), MAKELPARAM(n, 0));
}

static void poll_one(wcomm *c)
{
    UINT ev = 0;
    /* in: as much as there is, a few at a time */
    for (int k = 0; k < 64; k++) {
        uint8_t lsr = port_in((uint16_t)(c->base + LSR));
        if (lsr & 0x1e) {
            if (lsr & 0x02) c->error |= CE_OVERRUN;
            if (lsr & 0x04) c->error |= CE_RXPARITY;
            if (lsr & 0x08) c->error |= CE_FRAME;
            if (lsr & 0x10) {
                c->error |= CE_BREAK;
                ev |= EV_BREAK;
            }
            ev |= EV_ERR;
        }
        if (!(lsr & 1))
            break;
        uint8_t b = port_in((uint16_t)(c->base + RBR));
        if (c->dcb.fOutX && (b == (uint8_t)c->dcb.XoffChar || b == (uint8_t)c->dcb.XonChar)) {
            c->tx_held = b == (uint8_t)c->dcb.XoffChar;
            continue;
        }
        if (c->dcb.fNull && !b)
            continue;
        if (c->in_count == c->in_size) {
            c->error |= CE_RXOVER;
            continue;
        }
        c->in[(c->in_head + c->in_count) % c->in_size] = b;
        c->in_count++;
        ev |= EV_RXCHAR;
        if (b == (uint8_t)c->dcb.EvtChar)
            ev |= EV_RXFLAG;
    }
    /* XON/XOFF of its own: the input queue nearly full, then emptied */
    if (c->dcb.fInX) {
        if (!c->xoff_sent && c->in_size - c->in_count <= c->dcb.XoffLim &&
            send_now(c, (uint8_t)c->dcb.XoffChar))
            c->xoff_sent = 1;
        else if (c->xoff_sent && c->in_count <= c->dcb.XonLim &&
                 send_now(c, (uint8_t)c->dcb.XonChar))
            c->xoff_sent = 0;
    }
    /* out: as the transmitter takes it */
    int had = c->out_count != 0;
    for (int k = 0; k < 64 && c->out_count && may_send(c); k++) {
        if (!send_now(c, c->out[c->out_head]))
            break;
        c->out_head = (c->out_head + 1) % c->out_size;
        c->out_count--;
    }
    if (had && !c->out_count)
        ev |= EV_TXEMPTY;
    /* the modem's lines */
    uint8_t m = modem(c), d = (uint8_t)(m ^ c->msr);
    c->msr = m;
    if (d & 0x10) ev |= EV_CTS;
    if (d & 0x20) ev |= EV_DSR;
    if (d & 0x80) ev |= EV_RLSD;
    if ((d & 0x40) && !(m & 0x40)) ev |= EV_RING;
    notify(c, ev ? set_events(c, ev) : 0);
}

/* USER's poll: every serial port open */
void user_comm_poll(void)
{
    for (int i = 0; i < COMS; i++)
        if (coms[i].open)
            poll_one(&coms[i]);
}

/* a task gone: its ports closed */
void user_comm_task_ended(HTASK task)
{
    for (int i = 0; i < COMS; i++)
        if (coms[i].open && coms[i].task == task)
            CloseComm(i);
    for (int i = 0; i < LPTS; i++)
        if (lpts[i].open && lpts[i].task == task)
            CloseComm(0x80 | i);
}

/* what came in, up to n bytes; a negative count when an error is waiting
 * (GetCommError tells it) */
int WINAPI ReadComm(int cid, void *buf, int n)
{
    wcomm *c = comm_of(cid);
    if (!c || n < 0)
        return -1;
    if (c->base)
        poll_one(c);
    uint8_t *b = (uint8_t *)buf;
    int got = 0;
    if (c->unget >= 0 && n > 0) {
        b[got++] = (uint8_t)c->unget;
        c->unget = -1;
    }
    while (got < n && c->in_count) {
        b[got++] = c->in[c->in_head];
        c->in_head = (c->in_head + 1) % c->in_size;
        c->in_count--;
    }
    if (got)
        notify(c, 0);
    return c->error ? -got : got;
}

int WINAPI UngetCommChar(int cid, char ch)
{
    wcomm *c = comm_of(cid);
    if (!c || c->unget >= 0)
        return -1;
    c->unget = (uint8_t)ch;
    return 0;
}

/* to the output queue (what it holds); a parallel port's straight out */
int WINAPI WriteComm(int cid, const void *buf, int n)
{
    wcomm *c = comm_of(cid);
    if (!c || n < 0)
        return -1;
    if (!c->base) {
        UINT put = n ? _lwrite(c->dev, buf, (UINT)n) : 0;
        if (put == (UINT)HFILE_ERROR || put < (UINT)n) {
            c->error |= CE_IOE;
            return -(int)(put == (UINT)HFILE_ERROR ? 0 : put);
        }
        return n;
    }
    const uint8_t *b = (const uint8_t *)buf;
    int put = 0;
    while (put < n && c->out_count < c->out_size) {
        c->out[(c->out_head + c->out_count) % c->out_size] = b[put++];
        c->out_count++;
    }
    if (put < n)
        c->error |= CE_TXFULL;
    poll_one(c);
    return put < n ? -put : put;
}

/* ahead of the queue */
int WINAPI TransmitCommChar(int cid, char ch)
{
    wcomm *c = comm_of(cid);
    if (!c)
        return -1;
    if (!c->base)
        return WriteComm(cid, &ch, 1) == 1 ? 0 : -1;
    if (send_now(c, (uint8_t)ch))
        return 0;
    if (c->out_count == c->out_size)
        return -1;
    c->out_head = (c->out_head + c->out_size - 1) % c->out_size;
    c->out[c->out_head] = (uint8_t)ch;
    c->out_count++;
    return 0;
}

/* the errors (cleared) and the queues' state */
int WINAPI GetCommError(int cid, COMSTAT *st)
{
    wcomm *c = comm_of(cid);
    if (!c) {
        if (st)
            memset(st, 0, sizeof(*st));
        return CE_MODE;
    }
    if (c->base)
        poll_one(c);
    int e = (int)c->error;
    c->error = 0;
    if (st) {
        uint8_t m = modem(c);
        st->status = (BYTE)((c->dcb.fOutxCtsFlow && !(m & 0x10) ? CSTF_CTSHOLD : 0) |
                            (c->dcb.fOutxDsrFlow && !(m & 0x20) ? CSTF_DSRHOLD : 0) |
                            (c->tx_held ? CSTF_XOFFHOLD : 0) | (c->xoff_sent ? CSTF_XOFFSENT : 0));
        st->cbInQue = c->in_count + (c->unget >= 0);
        st->cbOutQue = c->out_count;
    }
    return e;
}

/* 0 the output queue, 1 the input */
int WINAPI FlushComm(int cid, int queue)
{
    wcomm *c = comm_of(cid);
    if (!c || queue < 0 || queue > 1)
        return -1;
    if (queue) {
        c->in_count = c->in_head = 0;
        c->unget = -1;
    } else {
        c->out_count = c->out_head = 0;
    }
    notify(c, 0);
    return 0;
}

LONG WINAPI EscapeCommFunction(int cid, int fn)
{
    if (fn == GETMAXCOM)
        return COMS - 1;
    if (fn == GETMAXLPT)
        return 0x80 | (LPTS - 1);
    wcomm *c = comm_of(cid);
    if (!c)
        return -1;
    if (fn == GETBASEIRQ)
        return c->base ? MAKELONG(c->base, (cid & 1) ? 3 : 4) : 0;
    if (!c->base)
        return fn == RESETDEV ? 0 : -1;
    uint8_t mcr = port_in((uint16_t)(c->base + MCR));
    switch (fn) {
    case SETXOFF: c->tx_held = 1; return 0;
    case SETXON: c->tx_held = 0; return 0;
    case SETRTS: mcr |= 2; break;
    case CLRRTS: mcr &= (uint8_t)~2; break;
    case SETDTR: mcr |= 1; break;
    case CLRDTR: mcr &= (uint8_t)~1; break;
    case RESETDEV: return 0;
    default: return -1;
    }
    port_out((uint16_t)(c->base + MCR), mcr);
    return 0;
}

static int set_break(int cid, int on)
{
    wcomm *c = comm_of(cid);
    if (!c)
        return -1;
    if (c->base) {
        c->brk = on;
        uint8_t lcr = port_in((uint16_t)(c->base + LCR));
        port_out((uint16_t)(c->base + LCR), (uint8_t)(on ? lcr | 0x40 : lcr & ~0x40));
    }
    return 0;
}

int WINAPI SetCommBreak(int cid) { return set_break(cid, 1); }
int WINAPI ClearCommBreak(int cid) { return set_break(cid, 0); }

/* the events to note; the word they are noted in */
UINT *WINAPI SetCommEventMask(int cid, UINT mask)
{
    wcomm *c = comm_of(cid);
    if (!c)
        return NULL;
    c->mask = mask;
    c->events &= mask;
    if (event_hook)
        event_hook(cid, c->events);
    return &c->events;
}

/* the events noted, those of clear cleared */
UINT WINAPI GetCommEventMask(int cid, int clear)
{
    wcomm *c = comm_of(cid);
    if (!c)
        return 0;
    if (c->base)
        poll_one(c);
    UINT e = c->events;
    c->events &= ~(UINT)clear;
    if (e != c->events && event_hook)
        event_hook(cid, c->events);
    return e;
}

/* WM_COMMNOTIFY to the window: CN_RECEIVE at rx bytes in (-1 never),
 * CN_TRANSMIT below tx bytes out (-1 never), CN_EVENT on the events */
BOOL WINAPI EnableCommNotification(int cid, HWND h, int rx, int tx)
{
    wcomm *c = comm_of(cid);
    if (!c || !c->base)
        return FALSE;
    c->notify = h;
    c->rx_notify = rx;
    c->tx_notify = tx;
    c->rx_told = c->tx_told = 0;
    return TRUE;
}
