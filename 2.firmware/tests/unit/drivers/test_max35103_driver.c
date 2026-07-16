#include "drivers/max35103/max35103.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    uint8_t frame[MAX35103_FLOW_FRAME_SIZE] = {
        0x01, 0x90, 0x00, 0x00, /* 100 us */
        0x01, 0x90, 0x00, 0x40, /* slightly longer */
        0xFF, 0xFF, 0xFF, 0xC0, /* AVGUP - AVGDN */
        0x03, 0x08              /* range=3, valid cycles=8 */
    };
    Max35103RawFlowSample sample;
    assert(max35103_decode_flow_frame(frame, sizeof(frame), &sample));
    assert(sample.tof_up_ps == 100000000);
    assert(sample.tof_down_ps > sample.tof_up_ps);
    assert(sample.tof_diff_ps < 0);
    assert(sample.valid_cycle_count == 8u);

    frame[12] = 0u; frame[13] = 0u;
    assert(!max35103_decode_flow_frame(frame, sizeof(frame), &sample));
    frame[8] = 0x00u; frame[9] = 0x00u;
    frame[10] = 0x00u; frame[11] = 0x00u;
    frame[13] = 1u;
    assert(!max35103_decode_flow_frame(frame, sizeof(frame), &sample));
    puts("MAX35103 Driver Tests: PASS");
    return 0;
}
