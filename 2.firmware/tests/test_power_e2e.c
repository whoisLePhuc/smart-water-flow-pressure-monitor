#include "adc_port_linux.h"
#include "event/app_event_queue.h"
#include "event/data_repository.h"
#include "facades/power_facade.h"
#include "services/telemetry_builder.h"
#include <assert.h>
#include <stdio.h>

static RuntimeSnapshot read_snapshot(DataRepository *repo)
{
    RuntimeSnapshot copy;
    assert(data_repository_snapshot_copy(repo, &copy));
    return copy;
}

int main(void)
{
    LinuxAdcAdapter adapter;
    AdcPort adc_port;
    AppEventQueue queue;
    DataRepository repo;
    PowerFacade power;
    PowerConfig config = (PowerConfig)POWER_CONFIG_DEFAULT;

    config.stale_count_max = 3u;
    adc_port_linux_init(&adapter, &adc_port);
    app_event_queue_init(&queue, NULL);
    data_repository_init(&repo);
    assert(power_facade_init(&power, &config, &adc_port, &repo, &queue)
           == PORT_OK);

    adc_port_linux_set_value(&adapter, 1600u);
    assert(power_facade_process_sample(&power, 1000000u) == PORT_OK);

    RuntimeSnapshot snapshot = read_snapshot(&repo);
    assert(snapshot.snapshot_version == 1u);
    assert(snapshot.power.available);
    assert(snapshot.power.raw_adc == 1600u);
    assert(snapshot.power.battery_mv == 3867u);
    assert(snapshot.power.health == POWER_STATE_NORMAL);
    assert(snapshot.power.sample_monotonic_us == 1000000u);
    assert(app_event_queue_get_count(&queue) == 1u);

    TelemetryBuilder builder;
    TelemetryRecord record;
    TelemetryBuilder_Init(&builder);
    assert(TelemetryBuilder_Build(&builder, &snapshot, 0u, 0, 0u,
                                  1000000u, 0, 0u, 1u, &record));
    assert(record.battery_mv == 3867u);

    adc_port_linux_set_fault(&adapter, PORT_STATUS_HARDWARE_ERROR);
    assert(power_facade_process_sample(&power, 2000000u)
           == PORT_STATUS_HARDWARE_ERROR);
    assert(power_facade_process_sample(&power, 3000000u)
           == PORT_STATUS_HARDWARE_ERROR);
    assert(power_facade_process_sample(&power, 4000000u)
           == PORT_STATUS_HARDWARE_ERROR);

    snapshot = read_snapshot(&repo);
    assert(snapshot.snapshot_version == 2u);
    assert(!snapshot.power.available);
    assert(snapshot.power.health == POWER_STATE_UNKNOWN);
    assert((snapshot.power.quality_flags & POWER_QUALITY_STALE) != 0u);
    assert(TelemetryBuilder_Build(&builder, &snapshot, 0u, 0, 0u,
                                  4000000u, 0, 0u, 1u, &record));
    assert(record.battery_mv == TELEMETRY_BATTERY_MV_UNAVAILABLE);

    puts("Power E2E: 1 passed, 0 failed");
    return 0;
}
