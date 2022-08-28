#ifndef APPLICATION_MODE_MINIMAL_H
#define APPLICATION_MODE_MINIMAL_H

#define APP_FW_VARIANT "+minimal"

#define APP_BLE_INTERVAL_MS (1285U * 4U) //5140 ms, closest to 5s divisible by 1285

#define APP_NUM_REPEATS 1

#define APP_BATTERY_SAMPLE_MS (5ULL*1000ULL)

#define APP_GATT_ENABLED (0U)

#define RT_FLASH_ENABLED (0U)

#endif