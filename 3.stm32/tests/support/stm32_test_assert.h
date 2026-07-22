#ifndef SWFPM_STM32_TEST_ASSERT_H
#define SWFPM_STM32_TEST_ASSERT_H

#include "support/stm32_test_runner.h"

#define STM32_TEST_REQUIRE(runner_, expression_)                         \
    do {                                                                \
        if (!(expression_)) {                                           \
            return stm32_test_fail((runner_), #expression_,             \
                                   __FILE__, __LINE__);                  \
        }                                                               \
    } while (0)

#define STM32_TEST_FAIL(runner_, message_)                               \
    return stm32_test_fail_message((runner_), (message_))

#endif /* SWFPM_STM32_TEST_ASSERT_H */
