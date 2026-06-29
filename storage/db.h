#ifndef STORAGE_DB_H
#define STORAGE_DB_H

#include <stdint.h>

/* 初始化/销毁 */
int  db_init   (const char *path);
void db_deinit (void);

/* 各传感器写入（从 embedmq 消费者线程调用） */
void db_log_dht11   (float temp, float humi);
void db_log_adxl345 (float x, float y, float z, float mag);
void db_log_sr501   (uint8_t detected);
void db_log_sr04    (float dist_cm);
void db_log_light   (uint16_t lux);
void db_log_comfort (float heat_index, uint8_t level);
void db_log_anomaly (uint8_t type, float magnitude);

/* 清理 keep_days 天前的旧记录（可在主线程调用） */
void db_cleanup_old (int keep_days);

/* 状态字符串，供 UI 展示 */
const char *db_status_str(void);

/* 返回 readings 表总行数，s_db 为 NULL 时返回 -1 */
int64_t db_count(void);

#endif /* STORAGE_DB_H */
