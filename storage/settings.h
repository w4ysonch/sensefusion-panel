#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

/* EEPROM 起始地址 */
#define SETTINGS_EEPROM_ADDR  0x0000

typedef struct {
    float    anomaly_threshold;  /* 异常检测阈值，单位 g，默认 0.3 */
    uint8_t  unit_fahrenheit;    /* 0=°C，1=°F */
    uint8_t  alert_muted;        /* 0=告警开，1=静音 */
    uint8_t  brightness;         /* 背光亮度 0~100 */
    uint32_t magic;              /* 校验魔数，0xSF2025CF 表示数据有效 */
} app_settings_t;

#define SETTINGS_MAGIC              0x5F2025CFu
#define SETTINGS_DEFAULT_THRESHOLD  0.3f        /* algo/anomaly.c 默认值与此保持同步 */

/* 从 EEPROM 加载，若无效则写入默认值 */
void settings_load(app_settings_t *s);
/* 持久化到 EEPROM */
int  settings_save(const app_settings_t *s);

#endif /* SETTINGS_H */
