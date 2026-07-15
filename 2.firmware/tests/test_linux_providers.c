/**
 * Platform provider contract tests
 * Tests: SPI, I2C, GPIO provider admission, completion, cancel, recovery
 */

#include "providers/linux_spi_provider.h"
#include "providers/linux_i2c_provider.h"
#include "providers/linux_gpio_provider.h"
#include "platform/include/linux_scheduled_action_queue.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  TEST: %s ... ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

/* ── Fake SPI peer ──────────────────────────────────── */

static bool fake_spi_plan(void *ctx, const uint8_t *tx, uint8_t *rx,
                           uint16_t len, uint64_t *latency, uint32_t *status)
{
    (void)ctx; (void)tx; (void)rx; (void)len;
    *latency = 100;  /* 100 us simulated transfer */
    *status = 0;
    return true;
}


/* ── Fake I2C peer ──────────────────────────────────── */

static bool fake_i2c_plan(void *ctx, uint8_t addr,
                           const uint8_t *tx, uint16_t tx_len,
                           uint8_t *rx, uint16_t rx_len,
                           uint64_t *latency, uint32_t *status)
{
    (void)ctx; (void)addr; (void)tx; (void)tx_len; (void)rx; (void)rx_len;
    *latency = 50;
    *status = 0;
    return true;
}

/* ── SPI tests ──────────────────────────────────────── */

static void test_spi_submit_accept(void)
{
    LinuxScheduledActionQueue q;
    action_queue_init(&q);

    LinuxSpiProvider spi;
    linux_spi_init(&spi, &q);
    linux_spi_register_peer(&spi, (LinuxSpiPeer){ .spi_plan = fake_spi_plan, .context = NULL });

    LinuxSpiRequest req = { .operation_id = 1, .correlation_id = 10, .owner_generation = 1, .deadline_us = 0 };
    bool ok = linux_spi_submit(&spi, &req);
    assert(ok);
    assert(spi.admission_accepted == 1);
    assert(q.count == 1);
    PASS();
}

static void test_spi_no_peer_rejected(void)
{
    LinuxScheduledActionQueue q;
    action_queue_init(&q);

    LinuxSpiProvider spi;
    linux_spi_init(&spi, &q);

    LinuxSpiRequest req = { .operation_id = 1, .correlation_id = 10, .owner_generation = 1 };
    bool ok = linux_spi_submit(&spi, &req);
    assert(!ok);
    assert(spi.admission_rejected == 1);
    PASS();
}

static void test_spi_cancel_by_generation(void)
{
    LinuxScheduledActionQueue q;
    action_queue_init(&q);

    LinuxSpiProvider spi;
    linux_spi_init(&spi, &q);
    linux_spi_register_peer(&spi, (LinuxSpiPeer){ .spi_plan = fake_spi_plan, .context = NULL });

    LinuxSpiRequest req = { .operation_id = 1, .correlation_id = 10, .owner_generation = 5, .deadline_us = 0 };
    linux_spi_submit(&spi, &req);

    /* Cancel with wrong generation */
    bool r = linux_spi_cancel(&spi, 1, 1);
    assert(!r);

    /* Cancel with correct generation */
    r = linux_spi_cancel(&spi, 1, 5);
    assert(r);
    PASS();
}

static void test_spi_recovery(void)
{
    LinuxScheduledActionQueue q;
    action_queue_init(&q);

    LinuxSpiProvider spi;
    linux_spi_init(&spi, &q);
    linux_spi_register_peer(&spi, (LinuxSpiPeer){ .spi_plan = fake_spi_plan, .context = NULL });

    uint32_t gen_before = spi.resource_generation;
    linux_spi_recover(&spi);
    assert(spi.resource_generation == gen_before + 1);
    PASS();
}

/* ── I2C tests ──────────────────────────────────────── */

static void test_i2c_register_peer(void)
{
    LinuxScheduledActionQueue q;
    action_queue_init(&q);

    LinuxI2cProvider i2c;
    linux_i2c_init(&i2c, &q);
    LinuxI2cPeer peer = { .i2c_plan = fake_i2c_plan, .context = NULL };

    assert(linux_i2c_register_peer(&i2c, 0x50, peer));
    assert(!linux_i2c_register_peer(&i2c, 0x50, peer));  /* Duplicate */
    assert(i2c.peer_count == 1);
    PASS();
}

static void test_i2c_submit_accept(void)
{
    LinuxScheduledActionQueue q;
    action_queue_init(&q);

    LinuxI2cProvider i2c;
    linux_i2c_init(&i2c, &q);
    linux_i2c_register_peer(&i2c, 0x50, (LinuxI2cPeer){ .i2c_plan = fake_i2c_plan });

    LinuxI2cRequest req = { .operation_id = 1, .correlation_id = 10, .owner_generation = 1,
                            .slave_address = 0x50, .deadline_us = 0 };
    assert(linux_i2c_submit(&i2c, &req));
    assert(i2c.admission_accepted == 1);
    PASS();
}

/* ── GPIO tests ─────────────────────────────────────── */

static void test_gpio_set_get_level(void)
{
    LinuxScheduledActionQueue q;
    action_queue_init(&q);

    LinuxGpioProvider gpio;
    linux_gpio_init(&gpio, &q, 4);

    linux_gpio_set_level(&gpio, 0, GPIO_LEVEL_HIGH);
    assert(linux_gpio_get_level(&gpio, 0) == GPIO_LEVEL_HIGH);
    assert(linux_gpio_get_level(&gpio, 1) == GPIO_LEVEL_LOW);
    PASS();
}

static void test_gpio_schedule_evidence(void)
{
    LinuxScheduledActionQueue q;
    action_queue_init(&q);

    LinuxGpioProvider gpio;
    linux_gpio_init(&gpio, &q, 4);

    bool ok = linux_gpio_assert_edge(&gpio, 2, GPIO_EDGE_RISING, 1000);
    assert(ok);
    assert(q.count == 1);
    assert(q.actions[0].due_us == 1000);
    PASS();
}

static void test_gpio_recovery_increments_generation(void)
{
    LinuxScheduledActionQueue q;
    action_queue_init(&q);

    LinuxGpioProvider gpio;
    linux_gpio_init(&gpio, &q, 4);

    uint32_t gen = gpio.line_generation[0];
    linux_gpio_recover_line(&gpio, 0);
    assert(gpio.line_generation[0] == gen + 1);
    PASS();
}

int main(void)
{
    printf("Platform Provider Contract Tests\n");
    printf("─────────────────────────────────\n");

    test_spi_submit_accept();
    test_spi_no_peer_rejected();
    test_spi_cancel_by_generation();
    test_spi_recovery();

    test_i2c_register_peer();
    test_i2c_submit_accept();

    test_gpio_set_get_level();
    test_gpio_schedule_evidence();
    test_gpio_recovery_increments_generation();

    printf("─────────────────────────────────\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
