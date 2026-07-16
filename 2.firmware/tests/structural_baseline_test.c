#include <stdio.h>
#include "event/data_model.h"
#include "event/data_repository.h"
#include "event/app_event_queue.h"
#include "event/system_fsm.h"

int main(void) {
    printf("RuntimeSnapshot:      %zu bytes\n", sizeof(RuntimeSnapshot));
    printf("AppEvent:             %zu bytes\n", sizeof(AppEvent));
    printf("DataRepository:       %zu bytes\n", sizeof(DataRepository));
    printf("AppEventQueue:        %zu bytes\n", sizeof(AppEventQueue));
    printf("VolumeState:          %zu bytes\n", sizeof(VolumeState));
    printf("LeakDetectionResult:  %zu bytes\n", sizeof(LeakDetectionResult));
    printf("SystemModeManager:    %zu bytes\n", sizeof(SystemModeManager));
    printf("ModeGuardContext:     %zu bytes\n", sizeof(ModeGuardContext));
    printf("SystemModeContext:    %zu bytes\n", sizeof(SystemModeContext));
    printf("ResultMetadata:       %zu bytes\n", sizeof(ResultMetadata));
    return 0;
}
