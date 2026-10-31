#include "rtos.h"
#include "app_memory.h"

int main(void)
{
    am_hal_cachectrl_config(&am_hal_cachectrl_defaults);
    am_hal_cachectrl_enable();
    am_bsp_external_pwr_on();
    am_bsp_low_power_init();
    /* BSP low-power init installs default bank/DAXI settings. Apply the watch
     * configuration AFTER that, before any allocator or RTOS object uses RAM. */
    uint32_t cpu = am_hal_pwrctrl_mcu_memory_config(
        (am_hal_pwrctrl_mcu_memory_config_t *)&i_DefaultMcuMemCfg);
    uint32_t shared = am_hal_pwrctrl_sram_config(
        (am_hal_pwrctrl_sram_memcfg_t *)&i_DefaultSRAMCfg);
    configASSERT(cpu == AM_HAL_STATUS_SUCCESS && shared == AM_HAL_STATUS_SUCCESS);
    app_memory_init();
    run_tasks();
    for (;;) {}
}
