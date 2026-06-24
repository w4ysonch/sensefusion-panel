#include <stdio.h>
#include <time.h>
#include <sqlite3.h>
#include "db.h"

static sqlite3     *s_db     = NULL;
static const char  *s_status = "未初始化";

/* WAL 模式 + NORMAL 同步：写延迟 < 1ms，适合 eMMC */
static const char *SCHEMA =
    "PRAGMA journal_mode=WAL;"
    "PRAGMA synchronous=NORMAL;"
    "CREATE TABLE IF NOT EXISTS readings ("
    "  id     INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  ts     INTEGER NOT NULL,"
    "  sensor TEXT    NOT NULL,"
    "  v1     REAL DEFAULT 0,"
    "  v2     REAL DEFAULT 0,"
    "  v3     REAL DEFAULT 0,"
    "  v4     REAL DEFAULT 0"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_sensor_ts ON readings(sensor, ts);";

int db_init(const char *path)
{
    if (sqlite3_open(path, &s_db) != SQLITE_OK) {
        fprintf(stderr, "[db] 打开 '%s' 失败: %s\n", path, sqlite3_errmsg(s_db));
        sqlite3_close(s_db);
        s_db     = NULL;
        s_status = "打开失败";
        return -1;
    }
    char *err = NULL;
    if (sqlite3_exec(s_db, SCHEMA, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "[db] 建表失败: %s\n", err);
        sqlite3_free(err);
        sqlite3_close(s_db);
        s_db     = NULL;
        s_status = "建表失败";
        return -1;
    }
    s_status = "已就绪";
    printf("[db] 打开 '%s' 成功\n", path);
    return 0;
}

void db_deinit(void)
{
    if (s_db) {
        sqlite3_close(s_db);
        s_db     = NULL;
        s_status = "已关闭";
    }
}

const char *db_status_str(void)
{
    return s_status;
}

/* 所有写入通过此函数，调用方在 embedmq 消费者线程（天然串行） */
static void log_row(const char *sensor,
                    double v1, double v2, double v3, double v4)
{
    if (!s_db) return;
    static const char SQL[] =
        "INSERT INTO readings(ts,sensor,v1,v2,v3,v4) VALUES(?,?,?,?,?,?);";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(s_db, SQL, -1, &stmt, NULL) != SQLITE_OK) return;
    sqlite3_bind_int64 (stmt, 1, (sqlite3_int64)time(NULL));
    sqlite3_bind_text  (stmt, 2, sensor, -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 3, v1);
    sqlite3_bind_double(stmt, 4, v2);
    sqlite3_bind_double(stmt, 5, v3);
    sqlite3_bind_double(stmt, 6, v4);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void db_log_dht11   (float temp, float humi)        { log_row("dht11",   temp, humi,  0, 0); }
void db_log_adxl345 (float x, float y, float z, float mag) { log_row("adxl345", x, y, z, mag); }
void db_log_sr501   (uint8_t det)                   { log_row("sr501",   det,  0, 0, 0); }
void db_log_sr04    (float dist_cm)                 { log_row("sr04",    dist_cm, 0, 0, 0); }
void db_log_light   (uint16_t lux)                  { log_row("light",   lux,  0, 0, 0); }
void db_log_comfort (float hi, uint8_t level)       { log_row("comfort", hi, level, 0, 0); }
void db_log_anomaly (uint8_t type, float mag)       { log_row("anomaly", type, mag, 0, 0); }

void db_cleanup_old(int keep_days)
{
    if (!s_db) return;
    static const char SQL[] = "DELETE FROM readings WHERE ts < ?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(s_db, SQL, -1, &stmt, NULL) != SQLITE_OK) return;
    sqlite3_bind_int64(stmt, 1,
        (sqlite3_int64)(time(NULL) - (time_t)keep_days * 86400));
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite3_exec(s_db, "PRAGMA wal_checkpoint(TRUNCATE);", NULL, NULL, NULL);
}
