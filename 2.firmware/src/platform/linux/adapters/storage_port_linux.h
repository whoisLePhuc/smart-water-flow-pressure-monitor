#ifndef SWFPM_STORAGE_PORT_LINUX_H
#define SWFPM_STORAGE_PORT_LINUX_H

#include <stdint.h>

#include "storage_port.h"

#define LINUX_STORAGE_CAPACITY_BYTES 512u

typedef struct {
    uint8_t memory[LINUX_STORAGE_CAPACITY_BYTES];
    StorageIoCompletionFn completion_fn;
    void *completion_context;
    uint32_t generation;
    bool active;
} LinuxStorageAdapter;

bool storage_port_linux_init(LinuxStorageAdapter *adapter,
                             StoragePort *port_out);

#endif /* SWFPM_STORAGE_PORT_LINUX_H */
