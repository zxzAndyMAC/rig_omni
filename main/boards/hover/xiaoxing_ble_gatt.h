#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Start 0xFFA0 GATT. Idempotent. Hover only. */
void xiaoxing_ble_gatt_start(void);

/** Arm a delayed start so MQTT/TLS and wake word can finish first. */
void xiaoxing_ble_gatt_start_delayed(void);

/** Stop advertising and release BT controller RAM for voice. */
void xiaoxing_ble_gatt_stop(void);

#ifdef __cplusplus
}
#endif
