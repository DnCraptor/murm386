/* Host regression for the launch-time hourglass, including nested WinExec. */
#include <assert.h>
#include <stdio.h>
#include "../core/wincore.h"

/* The cursor test runs without GDI/USER: keep rendering disabled. */
wsys win_sys;

BOOL WINAPI IntersectRect(LPRECT d, const RECT *a, const RECT *b)
{
    (void)d; (void)a; (void)b;
    return FALSE;
}

int main(void)
{
    win_sys.screen.width = 640;
    win_sys.screen.height = 480;
    cursor_init();
    const uint8_t *arrow = win_sys.cur_and;
    assert(arrow != NULL);

    cursor_wait_begin();
    const uint8_t *wait = win_sys.cur_and;
    assert(wait != NULL && wait != arrow);
    cursor_select_builtin(WCUR_IBEAM); /* simulated WM_SETCURSOR */
    assert(win_sys.cur_and == wait);

    cursor_wait_begin();               /* nested launch */
    cursor_select_builtin(WCUR_CROSS);
    cursor_wait_end();
    assert(win_sys.cur_and == wait);
    cursor_wait_end();
    const uint8_t *cross = win_sys.cur_and;
    assert(cross != arrow && cross != wait);

    cursor_wait_end();                 /* redundant restore is harmless */
    assert(win_sys.cur_and == cross);
    cursor_wait_begin();
    cursor_wait_end();
    assert(win_sys.cur_and == cross);

    static uint8_t custom_and[128], custom_xor[128];
    cursor_wait_begin();
    cursor_set_shape(custom_and, custom_xor, 7, 9);
    assert(win_sys.cur_and == wait);
    cursor_wait_end();
    assert(win_sys.cur_and == custom_and && win_sys.cur_xor == custom_xor);
    assert(win_sys.cur_hot_x == 7 && win_sys.cur_hot_y == 9);

    cursor_wait_begin();
    cursor_wait_end();
    assert(win_sys.cur_and == custom_and);
    puts("cursor wait: PASS");
    return 0;
}
