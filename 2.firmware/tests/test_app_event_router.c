/**
 * Event router unit tests
 * Tests: all event ranges route to correct owner
 */

#include "core/app_event_router.h"
#include "core/app_event_queue.h"
#include "core/app_event.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  TEST: %s ... ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

static void test_route_system_events(void)
{
    AppEvent evt;
    memset(&evt, 0, sizeof(evt));

    evt.id = EVT_INIT_COMPLETED;
    RouteResult r = route_event(&evt);
    assert(r.owner == EVENT_OWNER_SYSTEM_FSM);
    assert(r.handled);

    evt.id = EVT_CRITICAL_ERROR;
    r = route_event(&evt);
    assert(r.owner == EVENT_OWNER_SYSTEM_FSM);
    PASS();
}

static void test_route_measurement_events(void)
{
    AppEvent evt;
    memset(&evt, 0, sizeof(evt));

    evt.id = EVT_MAX_IRQ_ASSERTED;
    RouteResult r = route_event(&evt);
    assert(r.owner == EVENT_OWNER_MEASUREMENT);

    evt.id = EVT_FLOW_RESULT_READY;
    r = route_event(&evt);
    assert(r.owner == EVENT_OWNER_MEASUREMENT);

    evt.id = EVT_PRESSURE_SAMPLE_DUE;
    r = route_event(&evt);
    assert(r.owner == EVENT_OWNER_MEASUREMENT);
    PASS();
}

static void test_route_product_events(void)
{
    AppEvent evt;
    memset(&evt, 0, sizeof(evt));

    evt.id = EVT_VOLUME_UPDATED;
    RouteResult r = route_event(&evt);
    assert(r.owner == EVENT_OWNER_PRODUCT);

    evt.id = EVT_LEAK_STATE_CHANGED;
    r = route_event(&evt);
    assert(r.owner == EVENT_OWNER_PRODUCT);
    PASS();
}

static void test_route_i2c_events(void)
{
    AppEvent evt;
    memset(&evt, 0, sizeof(evt));

    evt.id = EVT_I2C_TRANSACTION_COMPLETED;
    RouteResult r = route_event(&evt);
    assert(r.owner == EVENT_OWNER_INFRASTRUCTURE);
    PASS();
}

static void test_route_config_storage_events(void)
{
    AppEvent evt;
    memset(&evt, 0, sizeof(evt));

    evt.id = EVT_CONFIG_COMMIT_COMPLETED;
    RouteResult r = route_event(&evt);
    assert(r.owner == EVENT_OWNER_CONFIG_STORAGE);
    PASS();
}

static void test_route_time_reporting_events(void)
{
    AppEvent evt;
    memset(&evt, 0, sizeof(evt));

    evt.id = EVT_REPORT_DUE;
    RouteResult r = route_event(&evt);
    assert(r.owner == EVENT_OWNER_TIME_REPORTING);
    PASS();
}

static void test_route_ble_cellular_events(void)
{
    AppEvent evt;
    memset(&evt, 0, sizeof(evt));

    evt.id = EVT_CONNECTIVITY_CHANGED;
    RouteResult r = route_event(&evt);
    assert(r.owner == EVENT_OWNER_BLE_CELLULAR);
    PASS();
}

static void test_route_display_health_events(void)
{
    AppEvent evt;
    memset(&evt, 0, sizeof(evt));

    evt.id = EVT_LCD_REFRESH_REQUESTED;
    RouteResult r = route_event(&evt);
    assert(r.owner == EVENT_OWNER_DISPLAY_HEALTH);
    PASS();
}

static void test_route_unknown_event(void)
{
    AppEvent evt;
    memset(&evt, 0, sizeof(evt));
    evt.id = (EventId)0xFFFF;  /* Invalid */

    RouteResult r = route_event(&evt);
    assert(!r.handled);
    assert(r.owner == EVENT_OWNER_UNKNOWN);
    PASS();
}

int main(void)
{
    printf("Event Router Tests\n");
    printf("──────────────────\n");

    test_route_system_events();
    test_route_measurement_events();
    test_route_product_events();
    test_route_i2c_events();
    test_route_config_storage_events();
    test_route_time_reporting_events();
    test_route_ble_cellular_events();
    test_route_display_health_events();
    test_route_unknown_event();

    printf("──────────────────\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
