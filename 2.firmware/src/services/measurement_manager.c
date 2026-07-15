#include "services/measurement_manager.h"
#include <string.h>

void measurement_manager_init(MeasurementManager *mgr,
                               AppEventQueue *event_queue,
                               DataRepository *repo)
{
    memset(mgr, 0, sizeof(*mgr));
    mgr->event_queue = event_queue;
    mgr->repo = repo;
    mgr->production_enabled = true;

    max35103_init(&mgr->max, event_queue);
    zssc3241_init(&mgr->zssc, event_queue);
}

bool measurement_manager_process_event(MeasurementManager *mgr,
                                        const AppEvent *event)
{
    if (!mgr || !event) return false;

    switch (event->id) {
    case EVT_MAX_IRQ_ASSERTED:
        max35103_on_irq(&mgr->max, event->monotonic_timestamp_us);
        mgr->max_cycles++;
        return true;

    case EVT_MAX_SPI_COMPLETED:
        /* SPI success — rx_data from payload */
        max35103_on_spi_completion(&mgr->max,
            event->correlation_id, true,
            event->payload, event->payload_size,
            event->monotonic_timestamp_us);
        return true;

    case EVT_MAX_SPI_FAILED:
        max35103_on_spi_completion(&mgr->max,
            event->correlation_id, false,
            NULL, 0,
            event->monotonic_timestamp_us);
        return true;

    case EVT_MAX_RESULT_TIMEOUT:
        max35103_on_timeout(&mgr->max, event->monotonic_timestamp_us);
        return true;

    case EVT_PRESSURE_SAMPLE_DUE:
        zssc3241_on_sample_due(&mgr->zssc, event->monotonic_timestamp_us);
        mgr->zssc_cycles++;
        return true;

    case EVT_PRESSURE_EOC_ASSERTED:
        zssc3241_on_eoc_asserted(&mgr->zssc, event->monotonic_timestamp_us);
        return true;

    case EVT_PRESSURE_POLL_DUE:
        zssc3241_on_poll_due(&mgr->zssc, event->monotonic_timestamp_us);
        return true;

    case EVT_PRESSURE_TIMEOUT:
        zssc3241_on_timeout(&mgr->zssc, event->monotonic_timestamp_us);
        return true;

    default:
        return false;
    }
}
