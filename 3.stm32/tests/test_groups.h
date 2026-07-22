#ifndef SWFPM_FRAM_TEST_GROUPS_H
#define SWFPM_FRAM_TEST_GROUPS_H

#include <stddef.h>

#include "support/stm32_test_platform.h"
#include "support/stm32_test_runner.h"

extern const Stm32TestGroup g_test_i2c_port_stm32_group;
extern const Stm32TestGroup g_test_i2c1_hal_contract_group;
extern const Stm32TestGroup g_test_fram_driver_group;
extern const Stm32TestGroup g_test_fram_error_recovery_group;
extern const Stm32TestGroup g_test_fram_shared_bus_group;
extern const Stm32TestGroup g_test_storage_boot_restore_group;
extern const Stm32TestGroup g_test_storage_interrupted_commit_group;
extern const Stm32TestGroup g_test_storage_soak_group;

void fram_test_groups_bind_platform(Stm32TestPlatform *platform);
const Stm32TestGroup *fram_test_groups(size_t *count_out);

#endif /* SWFPM_FRAM_TEST_GROUPS_H */
