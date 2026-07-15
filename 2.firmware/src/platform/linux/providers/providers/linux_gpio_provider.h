#ifndef SWFPM_LINUX_GPIO_PROVIDER_H
#define SWFPM_LINUX_GPIO_PROVIDER_H

#include <stdint.h>
#include <stdbool.h>
#include "platform/include/linux_scheduled_action_queue.h"

#define LINUX_GPIO_MAX_LINES 8

typedef enum {
    GPIO_LEVEL_LOW,
    GPIO_LEVEL_HIGH
} GpioLevel;

typedef enum {
    GPIO_EDGE_NONE,
    GPIO_EDGE_RISING,
    GPIO_EDGE_FALLING,
    GPIO_EDGE_BOTH
} GpioEdgeType;

/* ── GPIO evidence (scheduled via action queue) ──────────── */

typedef struct {
    uint32_t line_id;
    GpioEdgeType edge;
    uint64_t scheduled_at_us;
    uint32_t line_generation;
} LinuxGpioEvidence;

/* ── Provider ────────────────────────────────────────────── */

typedef struct {
    LinuxScheduledActionQueue *action_queue;
    uint32_t                   line_generation[LINUX_GPIO_MAX_LINES];
    GpioLevel                  line_level[LINUX_GPIO_MAX_LINES];
    GpioEdgeType               armed_edge[LINUX_GPIO_MAX_LINES];
    uint32_t                   line_count;
} LinuxGpioProvider;

void linux_gpio_init(LinuxGpioProvider *provider,
                     LinuxScheduledActionQueue *action_queue,
                     uint32_t num_lines);

bool linux_gpio_set_level(LinuxGpioProvider *provider,
                          uint32_t line_id, GpioLevel level);

GpioLevel linux_gpio_get_level(const LinuxGpioProvider *provider,
                               uint32_t line_id);

/* Schedule an edge assertion via action queue.
 * If the line already matches the edge, the evidence fires immediately
 * at current_time_us. */
bool linux_gpio_assert_edge(LinuxGpioProvider *provider,
                            uint32_t line_id,
                            GpioEdgeType edge,
                            uint64_t at_us);

/* Arm edge detection for a line. When armed, the next matching edge
 * generates evidence. */
bool linux_gpio_arm_edge(LinuxGpioProvider *provider,
                         uint32_t line_id,
                         GpioEdgeType edge);

/* Increment line generation — invalidates stale evidence. */
void linux_gpio_recover_line(LinuxGpioProvider *provider,
                             uint32_t line_id);

#endif
