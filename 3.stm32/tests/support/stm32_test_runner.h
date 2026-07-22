#ifndef SWFPM_STM32_TEST_RUNNER_H
#define SWFPM_STM32_TEST_RUNNER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    STM32_TEST_RUNNING = 0,
    STM32_TEST_PASSED,
    STM32_TEST_FAILED,
    STM32_TEST_SKIPPED
} Stm32TestResult;

struct Stm32TestRunner;

typedef void (*Stm32TestResetFn)(void *context);
typedef Stm32TestResult (*Stm32TestPollFn)(
    struct Stm32TestRunner *runner, void *context, uint64_t now_us);

typedef struct {
    const char *id;
    const char *name;
    uint64_t timeout_us;
    bool destructive;
    Stm32TestResetFn reset;
    Stm32TestPollFn poll;
    void *context;
} Stm32TestCase;

typedef struct {
    const char *name;
    const Stm32TestCase *cases;
    size_t count;
} Stm32TestGroup;

typedef struct {
    void *context;
    void (*write_line)(void *context, const char *line);
} Stm32TestReporter;

typedef struct Stm32TestRunner {
    const Stm32TestGroup *groups;
    size_t group_count;
    size_t group_index;
    size_t case_index;
    uint64_t case_started_us;
    uint32_t passed;
    uint32_t failed;
    uint32_t skipped;
    bool case_started;
    bool finished;
    Stm32TestReporter reporter;
    char detail[160];
} Stm32TestRunner;

void stm32_test_runner_init(Stm32TestRunner *runner,
                            const Stm32TestGroup *groups,
                            size_t group_count,
                            const Stm32TestReporter *reporter);
void stm32_test_runner_poll(Stm32TestRunner *runner, uint64_t now_us);
bool stm32_test_runner_finished(const Stm32TestRunner *runner);

Stm32TestResult stm32_test_fail(Stm32TestRunner *runner,
                                const char *expression,
                                const char *file,
                                int line);
Stm32TestResult stm32_test_fail_message(Stm32TestRunner *runner,
                                        const char *message);

#endif /* SWFPM_STM32_TEST_RUNNER_H */
