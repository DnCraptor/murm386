/* Windows NE detection must not depend on the emulated CPU generation. */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../core/ne.h"

int main(int argc, char **argv)
{
    assert(argc == 2);
    assert(win_ne_classify_launch(argv[1], 2) == WIN_NE_LAUNCH_REQUIRES_386);
    assert(win_ne_classify_launch(argv[1], 1) == WIN_NE_LAUNCH_REQUIRES_386);
    assert(win_ne_classify_launch(argv[1], 3) == WIN_NE_LAUNCH_READY);
    assert(win_ne_classify_launch(argv[1], 4) == WIN_NE_LAUNCH_READY);
    assert(win_ne_classify_launch("nonexistent_NE_file.exe", 2) ==
           WIN_NE_LAUNCH_OTHER);
    FILE *f = fopen(argv[1], "rb");
    assert(f);
    unsigned char image[1024];
    assert(fread(image, 1, sizeof image, f) == sizeof image);
    assert(!fclose(f));
    /* Valid NE header, but its CS and SS are the same segment: the
     * restricted 386 executor cannot stage that layout. */
    image[0x100+0x1a] = 1;
    image[0x100+0x1b] = 0;
    const char *tmp = "NELOOP-unsupported-fixture.exe";
    f = fopen(tmp, "wb");
    assert(f && fwrite(image, 1, sizeof image, f) == sizeof image);
    assert(!fclose(f));
    assert(win_ne_classify_launch(tmp, 2) == WIN_NE_LAUNCH_REQUIRES_386);
    assert(win_ne_classify_launch(tmp, 3) == WIN_NE_LAUNCH_UNSUPPORTED);
    assert(!remove(tmp));
    puts("Win16 NE vs Native EZ classification / 286 diagnostics: PASS");
    return 0;
}
