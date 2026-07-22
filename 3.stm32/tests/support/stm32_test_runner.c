#include "support/stm32_test_runner.h"

#include <stdio.h>
#include <string.h>

#ifndef SWFPM_ENABLE_DESTRUCTIVE_STORAGE_TESTS
#define SWFPM_ENABLE_DESTRUCTIVE_STORAGE_TESTS 0
#endif

static void emit(Stm32TestRunner *runner, const char *line)
{
    if (runner && runner->reporter.write_line)
        runner->reporter.write_line(runner->reporter.context, line);
}

static const Stm32TestCase *current_case(const Stm32TestRunner *runner)
{
    if (!runner || runner->group_index >= runner->group_count)
        return NULL;
    const Stm32TestGroup *group = &runner->groups[runner->group_index];
    return runner->case_index < group->count
        ? &group->cases[runner->case_index] : NULL;
}

static size_t total_cases(const Stm32TestGroup *groups, size_t group_count)
{
    size_t total = 0u;
    for (size_t index = 0u; index < group_count; ++index)
        total += groups[index].count;
    return total;
}

void stm32_test_runner_init(Stm32TestRunner *runner,
                            const Stm32TestGroup *groups,
                            size_t group_count,
                            const Stm32TestReporter *reporter)
{
    if (!runner)
        return;
    memset(runner, 0, sizeof(*runner));
    runner->groups = groups;
    runner->group_count = group_count;
    if (reporter)
        runner->reporter = *reporter;

    char line[96];
    (void)snprintf(line, sizeof(line),
                   "SUITE|fram_hil|START|tests=%lu|destructive=%u",
                   (unsigned long)total_cases(groups, group_count),
                   (unsigned)SWFPM_ENABLE_DESTRUCTIVE_STORAGE_TESTS);
    emit(runner, line);
}

static void advance_case(Stm32TestRunner *runner)
{
    runner->case_index++;
    while (runner->group_index < runner->group_count &&
           runner->case_index >= runner->groups[runner->group_index].count) {
        runner->group_index++;
        runner->case_index = 0u;
    }
    runner->case_started = false;
    runner->detail[0] = '\0';
}

static void finish_suite(Stm32TestRunner *runner)
{
    char line[128];
    runner->finished = true;
    (void)snprintf(line, sizeof(line),
                   "SUMMARY|passed=%lu|failed=%lu|skipped=%lu",
                   (unsigned long)runner->passed,
                   (unsigned long)runner->failed,
                   (unsigned long)runner->skipped);
    emit(runner, line);
}

void stm32_test_runner_poll(Stm32TestRunner *runner, uint64_t now_us)
{
    if (!runner || runner->finished)
        return;
    if (runner->group_index >= runner->group_count) {
        finish_suite(runner);
        return;
    }

    const Stm32TestCase *test = current_case(runner);
    if (!test || !test->poll) {
        runner->failed++;
        emit(runner, "TEST|invalid-registration|FAIL");
        advance_case(runner);
        return;
    }

    if (!runner->case_started) {
        runner->case_started = true;
        runner->case_started_us = now_us;
        runner->detail[0] = '\0';

        char line[192];
        (void)snprintf(line, sizeof(line), "TEST|%s|START|%s",
                       test->id, test->name);
        emit(runner, line);

        if (test->destructive && !SWFPM_ENABLE_DESTRUCTIVE_STORAGE_TESTS) {
            runner->skipped++;
            (void)snprintf(line, sizeof(line),
                           "TEST|%s|SKIP|destructive-disabled", test->id);
            emit(runner, line);
            advance_case(runner);
            return;
        }

        if (test->reset)
            test->reset(test->context);
    }

    Stm32TestResult result = STM32_TEST_RUNNING;
    if (test->timeout_us != 0u &&
        now_us - runner->case_started_us >= test->timeout_us) {
        result = stm32_test_fail_message(runner, "case-timeout");
    } else {
        result = test->poll(runner, test->context, now_us);
    }

    if (result == STM32_TEST_RUNNING)
        return;

    char line[256];
    const unsigned long elapsed =
        (unsigned long)(now_us - runner->case_started_us);
    if (result == STM32_TEST_PASSED) {
        runner->passed++;
        (void)snprintf(line, sizeof(line),
                       "TEST|%s|PASS|elapsed_us=%lu", test->id, elapsed);
    } else if (result == STM32_TEST_SKIPPED) {
        runner->skipped++;
        (void)snprintf(line, sizeof(line),
                       "TEST|%s|SKIP|%s", test->id,
                       runner->detail[0] ? runner->detail : "runtime-skip");
    } else {
        runner->failed++;
        (void)snprintf(line, sizeof(line),
                       "TEST|%s|FAIL|elapsed_us=%lu|%s", test->id, elapsed,
                       runner->detail[0] ? runner->detail : "unspecified");
    }
    emit(runner, line);
    advance_case(runner);
}

bool stm32_test_runner_finished(const Stm32TestRunner *runner)
{
    return runner && runner->finished;
}

Stm32TestResult stm32_test_fail(Stm32TestRunner *runner,
                                const char *expression,
                                const char *file,
                                int line)
{
    if (runner) {
        (void)snprintf(runner->detail, sizeof(runner->detail),
                       "assert=%s|file=%s|line=%d",
                       expression ? expression : "?",
                       file ? file : "?", line);
    }
    return STM32_TEST_FAILED;
}

Stm32TestResult stm32_test_fail_message(Stm32TestRunner *runner,
                                        const char *message)
{
    if (runner) {
        (void)snprintf(runner->detail, sizeof(runner->detail), "%s",
                       message ? message : "failure");
    }
    return STM32_TEST_FAILED;
}
