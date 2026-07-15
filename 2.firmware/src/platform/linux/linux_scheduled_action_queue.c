#include "platform/linux_scheduled_action_queue.h"
#include <string.h>

/* =================================================================
 * Total-order comparator
 *
 * Returns true if a should be dispatched before b.
 * ================================================================= */

static bool action_lt(const LinuxScheduledAction *a,
                      const LinuxScheduledAction *b)
{
    if (a->due_us != b->due_us)
        return a->due_us < b->due_us;
    if (a->action_class != b->action_class)
        return a->action_class < b->action_class;
    if (a->resource_id != b->resource_id)
        return a->resource_id < b->resource_id;
    if (a->resource_generation != b->resource_generation)
        return a->resource_generation < b->resource_generation;
    if (a->source_sequence != b->source_sequence)
        return a->source_sequence < b->source_sequence;
    return a->insertion_sequence < b->insertion_sequence;
}

/* =================================================================
 * Min-heap helpers
 * ================================================================= */

static void heap_swap(LinuxScheduledAction *a, LinuxScheduledAction *b)
{
    LinuxScheduledAction tmp = *a;
    *a = *b;
    *b = tmp;
}

static void heap_sift_up(LinuxScheduledAction *heap, uint16_t idx)
{
    while (idx > 0) {
        uint16_t parent = (idx - 1) / 2;
        if (action_lt(&heap[idx], &heap[parent])) {
            heap_swap(&heap[idx], &heap[parent]);
            idx = parent;
        } else {
            break;
        }
    }
}

static void heap_sift_down(LinuxScheduledAction *heap, uint16_t count, uint16_t idx)
{
    for (;;) {
        uint16_t smallest = idx;
        uint16_t left = 2 * idx + 1;
        uint16_t right = 2 * idx + 2;

        if (left < count && action_lt(&heap[left], &heap[smallest]))
            smallest = left;
        if (right < count && action_lt(&heap[right], &heap[smallest]))
            smallest = right;

        if (smallest != idx) {
            heap_swap(&heap[idx], &heap[smallest]);
            idx = smallest;
        } else {
            break;
        }
    }
}

/* =================================================================
 * API implementation
 * ================================================================= */

void action_queue_init(LinuxScheduledActionQueue *queue)
{
    memset(queue, 0, sizeof(*queue));
}

bool action_queue_schedule(LinuxScheduledActionQueue *queue,
                           const LinuxScheduledAction *action)
{
    if (!queue || !action) return false;

    if (queue->count >= ACTION_QUEUE_MAX_CAPACITY) {
        queue->overflow_count++;
        return false;
    }

    uint16_t idx = queue->count;
    queue->actions[idx] = *action;
    queue->actions[idx].insertion_sequence = queue->next_insertion_seq++;
    queue->count++;

    heap_sift_up(queue->actions, idx);
    return true;
}

bool action_queue_cancel(LinuxScheduledActionQueue *queue,
                         uint32_t operation_id,
                         uint32_t expected_generation)
{
    if (!queue) return false;

    /* Linear scan is acceptable for bounded embedded queue size (max 64).
     * For larger queues, use a hash or index. */
    for (uint16_t i = 0; i < queue->count; i++) {
        if (queue->actions[i].operation_id == operation_id) {
            if (queue->actions[i].owner_generation != expected_generation) {
                queue->stale_cancel_count++;
                return false;
            }
            /* Remove by swapping with last and sifting down */
            queue->cancel_count++;
            queue->actions[i] = queue->actions[queue->count - 1];
            queue->count--;
            if (i < queue->count) {
                heap_sift_down(queue->actions, queue->count, i);
            }
            return true;
        }
    }
    return false;
}

uint16_t action_queue_dispatch_due(LinuxScheduledActionQueue *queue,
                                   uint64_t now_us,
                                   LinuxScheduledAction *out,
                                   uint16_t max_out)
{
    if (!queue || !out || max_out == 0) return 0;

    uint16_t dispatched = 0;

    while (queue->count > 0 && dispatched < max_out) {
        LinuxScheduledAction *top = &queue->actions[0];

        if (top->due_us > now_us)
            break;  /* No more due actions */

        /* Copy to output */
        out[dispatched] = *top;
        dispatched++;
        queue->total_dispatched++;

        /* Remove min element */
        queue->actions[0] = queue->actions[queue->count - 1];
        queue->count--;
        if (queue->count > 0) {
            heap_sift_down(queue->actions, queue->count, 0);
        }
    }

    return dispatched;
}

bool action_queue_next_deadline(const LinuxScheduledActionQueue *queue,
                                uint64_t *deadline_us)
{
    if (!queue || !deadline_us || queue->count == 0)
        return false;

    *deadline_us = queue->actions[0].due_us;
    return true;
}

uint16_t action_queue_get_count(const LinuxScheduledActionQueue *queue)
{
    return queue ? queue->count : 0;
}

uint32_t action_queue_get_overflow(const LinuxScheduledActionQueue *queue)
{
    return queue ? queue->overflow_count : 0;
}
