#include <assert.h>
#include <stdio.h>
#include "../core/ne_frame_locals.h"
static void check_slot(const win_ne_frame_locals *f,unsigned o,unsigned w,
                       unsigned tag,int known,unsigned value)
{
    unsigned t=0;int k=0;uint16_t v=0;
    assert(win_ne_local_access(f,o,w));
    win_ne_local_load(f,o,w,&t,&k,&v);
    assert(t==tag && k==known && (!known || v==value));
}
int main(void)
{
    win_ne_frame_locals f={0};
    /* Saved BX occupies BP-2 and is NOT local; SUB reserves BP-4 only. */
    assert(win_ne_local_reserve(&f,2,2));
    assert(!win_ne_local_access(&f,2,2));
    assert(!win_ne_local_access(&f,3,2));
    assert(win_ne_local_access(&f,4,2));
    win_ne_local_store(&f,4,2,WIN_NE_LOCAL_WORD,1,0x1234);
    check_slot(&f,4,2,WIN_NE_LOCAL_WORD,1,0x1234);
    check_slot(&f,3,1,WIN_NE_LOCAL_WORD,1,0x12);
    check_slot(&f,4,1,WIN_NE_LOCAL_WORD,1,0x34);
    win_ne_local_store(&f,4,1,WIN_NE_LOCAL_WORD,1,0x56);
    check_slot(&f,4,2,WIN_NE_LOCAL_WORD,1,0x1256);
    win_ne_local_store(&f,3,1,WIN_NE_LOCAL_WORD,1,0x78);
    check_slot(&f,4,2,WIN_NE_LOCAL_WORD,1,0x7856);
    win_ne_local_store(&f,4,2,WIN_NE_LOCAL_DS,0,0);
    check_slot(&f,4,2,WIN_NE_LOCAL_DS,0,0);
    win_ne_local_store(&f,4,1,WIN_NE_LOCAL_WORD,1,5);
    check_slot(&f,4,2,WIN_NE_LOCAL_WORD,0,0);
    assert(!win_ne_local_release(&f,4,4));
    assert(win_ne_local_release(&f,4,2));
    assert(!win_ne_local_access(&f,4,2));
    /* Two locals with an unaligned word crossing their boundary. */
    assert(win_ne_local_reserve(&f,0,4));
    win_ne_local_store(&f,2,2,WIN_NE_LOCAL_WORD,1,0xabcd);
    win_ne_local_store(&f,4,2,WIN_NE_LOCAL_WORD,1,0xef12);
    assert(win_ne_local_access(&f,3,2));
    win_ne_local_store(&f,3,2,WIN_NE_LOCAL_WORD,1,0x8888);
    check_slot(&f,2,2,WIN_NE_LOCAL_WORD,0,0);
    check_slot(&f,4,2,WIN_NE_LOCAL_WORD,0,0);
    assert(win_ne_local_release(&f,4,4));
    /* Copied frame state must not alias its origin (near CALL). */
    assert(win_ne_local_reserve(&f,0,2));
    win_ne_local_store(&f,2,2,WIN_NE_LOCAL_WORD,1,0x0020);
    win_ne_frame_locals saved=f;
    assert(win_ne_local_release(&f,2,2));
    assert(win_ne_local_reserve(&f,0,2));
    win_ne_local_store(&f,2,2,WIN_NE_LOCAL_WORD,1,0x0099);
    f=saved;
    check_slot(&f,2,2,WIN_NE_LOCAL_WORD,1,0x0020);
    /* Caller-managed literal stack slots must NEVER grant local access. */
    win_ne_frame_locals none={0};
    assert(!win_ne_local_access(&none,2,2));
    assert(!win_ne_local_release(&none,2,2));
    assert(!win_ne_local_reserve(&none,20,2));
    puts("WINN BP locals / callee-save isolation / symbolic values: PASS");
    return 0;
}
