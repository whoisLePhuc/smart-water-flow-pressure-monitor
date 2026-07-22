#include "test_groups.h"

void test_i2c1_hal_contract_bind_platform(Stm32TestPlatform *platform);
void test_fram_driver_bind_platform(Stm32TestPlatform *platform);
void test_fram_error_recovery_bind_platform(Stm32TestPlatform *platform);
void test_fram_shared_bus_bind_platform(Stm32TestPlatform *platform);
void test_storage_boot_restore_bind_platform(Stm32TestPlatform *platform);
void test_storage_interrupted_commit_bind_platform(
    Stm32TestPlatform *platform);
void test_storage_soak_bind_platform(Stm32TestPlatform *platform);

static Stm32TestGroup s_groups[8];
static bool s_groups_ready;

void fram_test_groups_bind_platform(Stm32TestPlatform *platform)
{
    test_i2c1_hal_contract_bind_platform(platform);
    test_fram_driver_bind_platform(platform);
    test_fram_error_recovery_bind_platform(platform);
    test_fram_shared_bus_bind_platform(platform);
    test_storage_boot_restore_bind_platform(platform);
    test_storage_interrupted_commit_bind_platform(platform);
    test_storage_soak_bind_platform(platform);

    s_groups[0] = g_test_i2c_port_stm32_group;
    s_groups[1] = g_test_i2c1_hal_contract_group;
    s_groups[2] = g_test_fram_driver_group;
    s_groups[3] = g_test_fram_error_recovery_group;
    s_groups[4] = g_test_fram_shared_bus_group;
    s_groups[5] = g_test_storage_boot_restore_group;
    s_groups[6] = g_test_storage_interrupted_commit_group;
    s_groups[7] = g_test_storage_soak_group;
    s_groups_ready = true;
}

const Stm32TestGroup *fram_test_groups(size_t *count_out)
{
    if (count_out)
        *count_out = s_groups_ready ? sizeof(s_groups) / sizeof(s_groups[0])
                                    : 0u;
    return s_groups_ready ? s_groups : NULL;
}
