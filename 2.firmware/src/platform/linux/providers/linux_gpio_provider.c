#include "platform/providers/linux_gpio_provider.h"
#include <string.h>

void linux_gpio_init(LinuxGpioProvider *provider,
                     LinuxScheduledActionQueue *action_queue,
                     uint32_t num_lines)
{
    memset(provider, 0, sizeof(*provider));
    provider->action_queue = action_queue;
    provider->line_count = (num_lines < LINUX_GPIO_MAX_LINES)
                           ? num_lines : LINUX_GPIO_MAX_LINES;
    for (uint32_t i = 0; i < provider->line_count; i++) {
        provider->line_generation[i] = 1;
        provider->line_level[i] = GPIO_LEVEL_LOW;
        provider->armed_edge[i] = GPIO_EDGE_NONE;
    }
}

bool linux_gpio_set_level(LinuxGpioProvider *provider,
                          uint32_t line_id, GpioLevel level)
{
    if (!provider || line_id >= provider->line_count) return false;
    provider->line_level[line_id] = level;
    return true;
}

GpioLevel linux_gpio_get_level(const LinuxGpioProvider *provider,
                               uint32_t line_id)
{
    if (!provider || line_id >= provider->line_count)
        return GPIO_LEVEL_LOW;
    return provider->line_level[line_id];
}

bool linux_gpio_arm_edge(LinuxGpioProvider *provider,
                         uint32_t line_id, GpioEdgeType edge)
{
    if (!provider || line_id >= provider->line_count) return false;
    provider->armed_edge[line_id] = edge;
    return true;
}

bool linux_gpio_assert_edge(LinuxGpioProvider *provider,
                            uint32_t line_id,
                            GpioEdgeType edge,
                            uint64_t at_us)
{
    if (!provider || line_id >= provider->line_count) return false;

    LinuxScheduledAction action;
    memset(&action, 0, sizeof(action));
    action.due_us = at_us;
    action.action_class = ACTION_CLASS_GPIO_EVIDENCE;
    action.resource_id = line_id;
    action.resource_generation = provider->line_generation[line_id];
    action.source_sequence = (uint32_t)edge;
    action.detail_flags = (uint32_t)edge;

    return action_queue_schedule(provider->action_queue, &action);
}

void linux_gpio_recover_line(LinuxGpioProvider *provider,
                             uint32_t line_id)
{
    if (!provider || line_id >= provider->line_count) return;
    provider->line_generation[line_id]++;
}
