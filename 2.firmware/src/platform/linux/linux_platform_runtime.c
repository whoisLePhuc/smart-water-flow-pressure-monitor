#include "platform/include/platform_runtime.h"
#include <stdio.h>

void platform_init(void)
{
    fprintf(stdout, "[PLATFORM] Linux simulation platform initialized\n");
    fflush(stdout);
}

bool platform_poll(void)
{
    /* In Phase 1, platform_poll is a no-op.
     * Future phases will inject emulator events here. */
    return false;
}
