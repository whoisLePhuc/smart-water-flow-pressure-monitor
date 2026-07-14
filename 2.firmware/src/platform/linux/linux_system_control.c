#include "platform/system_control_port.h"
#include <stdio.h>
#include <stdlib.h>

void system_request_reset(uint32_t reason)
{
    /* In simulation: log reason and terminate */
    fprintf(stdout, "[SYSTEM] Reset requested. Reason code: 0x%08X\n", reason);
    fflush(stdout);
    exit(0);
}
