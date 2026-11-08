#ifndef HOST_DRIVERS_H
#define HOST_DRIVERS_H
#include "winddi.h"
extern const win_display_driver host_snap_display;
extern const win_input_driver host_script_input;
int  host_snapshot(const char *path);
void host_push_key(int vk, int sc, int down);
void host_push_mouse(int x, int y, unsigned btn);
void host_advance(uint32_t ms);
int  host_pending(void);
#endif
