/*
 * user.h - internal structures of USER (windows, messages, controls).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef MURM_USER_H
#define MURM_USER_H

#include "wincore.h"

typedef struct wclass {
    struct wclass *next;
    char     *name;
    ATOM      atom;
    WNDCLASS  wc;
    uint8_t  *extra;             /* cbClsExtra bytes (SetClassWord 0..) */
} wclass;

typedef struct wprop {
    struct wprop *next;
    ATOM    atom;
    HANDLE  data;
} wprop;

typedef struct wscroll {
    int min, max, pos;
} wscroll;

typedef struct wwnd {
    HWND      h;
    wclass   *cls;
    WNDPROC   proc;
    DWORD     style, exstyle;
    struct wwnd *parent, *owner;
    struct wwnd *child;          /* topmost child */
    struct wwnd *next;           /* next sibling below */
    RECT      rc, client;        /* screen coordinates */
    RECT      normal;            /* restored position (parent client coords) */
    char     *text;
    UINT      id;
    HINSTANCE inst;
    uint8_t  *extra;
    int       cb_extra;
    wrgn      update;            /* screen coordinates */
    int       erase, ncpaint;
    wprop    *props;
    wscroll   vs, hs;
    HDC       owndc;
    int       destroying;
    int       active_frame;      /* caption drawn active */
    HCURSOR   cursor_override;
    HMENU     menu;              /* menu bar (top-level windows) */
    HMENU     sysmenu;           /* own copy after GetSystemMenu */
    int       alt_tap;           /* Alt pressed with nothing else yet */
    HTASK     task;              /* owner task; 0 = the desktop */
    int       dialog;            /* made by the dialog manager */
    int       need_size;         /* overlapped: WM_SIZE, WM_MOVE at its first ShowWindow */
} wwnd;

/* Global USER state. */
typedef struct wuser {
    wwnd    *desktop;
    wwnd    *active, *focus, *capture;
    uint8_t  keys[256];          /* bit7 down, bit0 toggled */
    int      mouse_x, mouse_y;
    unsigned buttons;
    MSG      cur_msg;            /* last retrieved */
    int      in_modal_move;
    int      shutting_down;     /* accepted WM_QUERYENDSESSION: no more repaint */
    int      layout;             /* 0 = US, 1 = Russian */
    uint32_t last_click_time;
    int      last_click_x, last_click_y, last_click_msg;
    HWND     last_click_hwnd;
} wuser;

extern wuser U;

/* Metrics (VGA, Windows 3.1 look). */
#define UM_BORDER     1
#define UM_FRAME      4
#define UM_CAPTION    18      /* caption bar height, separator line extra */
#define UM_SCROLL     17
#define UM_MENU       18      /* menu bar line, black line below extra */
#define UM_ICON       32
#define UM_ICONW      76      /* minimized window: icon and a title of up to two lines */
#define UM_ICONH      70
#define UM_ICONGAP    4       /* free space between icon places */

/* wnd.c */
wwnd   *user_wnd(HWND h);
wwnd   *user_toplevel(wwnd *w);
int     user_is_visible(const wwnd *w);
void    user_nc_rects(const wwnd *w, RECT *client);   /* client from rc */
int     user_frame_width(DWORD style, DWORD exstyle);
int     user_has_caption(DWORD style);
void    user_vis_region(const wwnd *w, int client_only, wrgn *out);
void    user_invalidate_screen(const wrgn *r);
void    user_invalidate(wwnd *w, const wrgn *r, int erase, int children);
void    user_validate(wwnd *w, const wrgn *r);
wclass *user_find_class(LPCSTR name);
void    user_unregister_task_classes(HTASK task);
void    user_register_builtin(LPCSTR name, WNDPROC proc, UINT style, int extra,
                              HCURSOR cur, HBRUSH bg);
void    user_init_windows(void);
void    user_bring_to_top(wwnd *w);
wwnd   *user_root_owner(wwnd *w);
wwnd   *user_enabled_popup(wwnd *w);
void    user_switch_to(wwnd *w);
void    user_icon_slot(wwnd *w, RECT *r);     /* free place for w minimized */
wwnd   *user_window_from_point(int x, int y, int *hit);
void    user_set_window_rect(wwnd *w, const RECT *rc, UINT flags);
void    user_dlg_focus_changed(wwnd *w);
int     user_buttons_swapped(void);

/* msg.c */
void    user_post(HWND h, UINT m, WPARAM wp, LPARAM lp);
void    user_purge_window(HWND h);
void    user_kill_window_timers(HWND h);
int     user_pump_one(void);        /* modal loops: one message, 0 on WM_QUIT */

/* paint.c */
wwnd   *user_find_paint(HTASK task);    /* desktop windows count for any task */
HDC     user_get_dc(wwnd *w, int whole, const wrgn *limit);

/* nc.c */
LRESULT user_nc_hittest(wwnd *w, int x, int y);
void    user_nc_paint(wwnd *w);
void    user_overlay_icons(HWND source, const RECT *damage);
void    user_nc_button(wwnd *w, UINT msg, int hit, int x, int y);
void    user_syscommand(wwnd *w, UINT cmd, int x, int y);
void    user_alt_tab(wwnd *w);            /* fast switching, Alt held */
void    user_draw_3d(HDC dc, const RECT *rc, int pressed);
void    user_draw_arrow(HDC dc, const RECT *rc, int dir, COLORREF c);
void    user_scroll_set(wwnd *w, int bar, int min, int max, int pos, int redraw);
void    user_scroll_ctl_track(wwnd *w, int x, int y);

/* menu.c */
int     user_menu_bar_height(const wwnd *w);           /* 0 or UM_MENU + 1 */
void    user_menu_paint_bar(wwnd *w, HDC wdc, const RECT *bar);
void    user_menu_track_bar(wwnd *w, int x, int y);    /* mouse on the bar */
void    user_menu_key(wwnd *w, int ch);                /* SC_KEYMENU */
void    user_menu_track_sys(wwnd *w, int by_key);
void    user_menu_destroy_window(wwnd *w);
int     user_translate_accel(HWND h, HACCEL a, MSG *m);

/* sysmet.c */
HBRUSH  user_sys_brush(int index);
void    user_init_sysmet(void);
void    user_load_colors(void);             /* WIN.INI [colors] */
void    user_load_desktop(void);            /* WIN.INI [Desktop] Pattern */
HBRUSH  user_desktop_pattern(void);         /* 0: plain colour */
void    user_draw_wallpaper(HDC dc, const RECT *rc);   /* the desktop's bitmap, if any */
HBITMAP user_load_bmp(const char *name, int *w, int *h);  /* a .BMP file, its size */
void    user_repaint_all(void);
int     user_draw_text(HDC dc, LPCSTR s, int n, RECT *rc, UINT fmt);

/* controls */
LRESULT CALLBACK user_button_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK user_static_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK user_edit_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK user_listbox_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK user_dialog_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK user_desktop_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK user_menu_popup_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK user_scrollbar_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK user_combo_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK user_mdiclient_proc(HWND, UINT, WPARAM, LPARAM);

/* control helpers */
void    user_dialog_menu(HMENU menu);   /* the next dialog's menu */
/* the programs' hooks: the types there are (bits USER_HOOK_BIT(WH_...)),
 * what calls them: WH_CALLWNDPROC with a CWPSTRUCT of the message sent;
 * WH_CBT's HCBT_CREATEWND, _DESTROYWND, _ACTIVATE, _SETFOCUS, _MINMAX,
 * _SYSCOMMAND (nonzero: it does not happen); WH_KEYBOARD, WH_MOUSE with
 * the input a task takes (nonzero: it is thrown away); WH_GETMESSAGE
 * with the MSG retrieved; WH_MSGFILTER, WH_SYSMSGFILTER with a modal
 * loop's (CallMsgFilter); WH_SHELL with a top-level window made, gone */
#ifndef USER_HOOK_BIT
#define USER_HOOK_BIT(type) (1u << ((type) + 1))
#endif
void    user_set_hooks(unsigned types, LRESULT (*call)(int type, int code, WPARAM wp, LPARAM lp));
LRESULT user_hook(int type, int code, WPARAM wp, LPARAM lp);
HFONT   user_ctl_font(HWND h);
int     user_ctl_line(HWND h);      /* its font's line height: 16 the system font's */
HBRUSH  user_ctl_color(HWND h, HDC dc, int type);

/* keyboard.c */
int     user_vk_to_char(int vk, int sc, unsigned char *out);
/* PeekMessage for USER's own loops, which idle themselves */
BOOL    user_peek(LPMSG msg, HWND h, UINT first, UINT last, UINT flags);

/* clipboard.c */
void    user_clipboard_task_ended(HTASK task);
int     user_clipboard_put_text(HWND h, const char *s, int n);
char   *user_clipboard_get_text(HWND h);     /* win_free it */

/* caret.c */
void    user_caret_hide_for_paint(wwnd *w);
void    user_caret_restore(wwnd *w);
void    user_caret_tick(uint32_t now);
void    user_caret_window_gone(HWND h);

#endif /* MURM_USER_H */
