#ifndef SWFPM_CRITICAL_SECTION_PORT_H
#define SWFPM_CRITICAL_SECTION_PORT_H

#include <stdbool.h>
#include <stdint.h>

typedef uintptr_t CriticalSectionState;

typedef CriticalSectionState (*CriticalSectionEnterFn)(void *context,
                                                        bool from_isr);
typedef void (*CriticalSectionExitFn)(void *context,
                                      CriticalSectionState previous_state,
                                      bool from_isr);

typedef struct {
    void *context;
    CriticalSectionEnterFn enter;
    CriticalSectionExitFn exit;
} CriticalSectionPort;

static inline bool critical_section_port_is_valid(
    const CriticalSectionPort *port)
{
    return port && port->enter && port->exit;
}

#endif /* SWFPM_CRITICAL_SECTION_PORT_H */
