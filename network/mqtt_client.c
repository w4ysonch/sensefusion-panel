#ifdef MQTT_ENABLED

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <mosquitto.h>
#include "mqtt_client.h"

#define MQTT_QOS       0
#define MQTT_RETAIN    false
#define BUF_SIZE       128

static struct mosquitto *s_mosq   = NULL;
static const char       *s_status = "未初始化";

static void on_connect(struct mosquitto *mosq, void *ud, int rc)
{
    (void)mosq; (void)ud;
    if (rc == 0) {
        s_status = "已连接";
        printf("[mqtt] 已连接到 broker\n");
    } else {
        s_status = "连接失败";
        fprintf(stderr, "[mqtt] 连接失败，code=%d\n", rc);
    }
}

static void on_disconnect(struct mosquitto *mosq, void *ud, int rc)
{
    (void)mosq; (void)ud;
    s_status = "已断开";
    if (rc != 0)
        fprintf(stderr, "[mqtt] 意外断开，code=%d\n", rc);
}

int mqtt_init(const char *host, int port, const char *client_id)
{
    mosquitto_lib_init();
    s_mosq = mosquitto_new(client_id, true, NULL);
    if (!s_mosq) {
        fprintf(stderr, "[mqtt] mosquitto_new 失败\n");
        s_status = "初始化失败";
        return -1;
    }
    mosquitto_connect_callback_set(s_mosq, on_connect);
    mosquitto_disconnect_callback_set(s_mosq, on_disconnect);

    int rc = mosquitto_connect_async(s_mosq, host, port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "[mqtt] connect_async 失败: %s\n", mosquitto_strerror(rc));
        mosquitto_destroy(s_mosq);
        s_mosq   = NULL;
        s_status = "连接失败";
        return -1;
    }
    /* mosquitto_loop_start 启动内部后台线程，publish 线程安全 */
    mosquitto_loop_start(s_mosq);
    s_status = "连接中";
    printf("[mqtt] 正在连接 %s:%d ...\n", host, port);
    return 0;
}

void mqtt_deinit(void)
{
    if (s_mosq) {
        mosquitto_loop_stop(s_mosq, true);
        mosquitto_disconnect(s_mosq);
        mosquitto_destroy(s_mosq);
        s_mosq   = NULL;
        s_status = "已关闭";
        mosquitto_lib_cleanup();
    }
}

const char *mqtt_status_str(void)
{
    return s_status;
}

static void publish(const char *suffix, const char *json)
{
    if (!s_mosq) return;
    char topic[64];
    snprintf(topic, sizeof(topic), MQTT_TOPIC_PREFIX "/%s", suffix);
    mosquitto_publish(s_mosq, NULL, topic,
                      (int)strlen(json), json, MQTT_QOS, MQTT_RETAIN);
}

void mqtt_publish_dht11(float temp, float humi)
{
    char buf[BUF_SIZE];
    snprintf(buf, sizeof(buf),
        "{\"ts\":%ld,\"temp\":%.1f,\"humi\":%.1f}", time(NULL), temp, humi);
    publish("dht11", buf);
}

void mqtt_publish_adxl345(float x, float y, float z, float mag)
{
    char buf[BUF_SIZE];
    snprintf(buf, sizeof(buf),
        "{\"ts\":%ld,\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,\"mag\":%.3f}",
        time(NULL), x, y, z, mag);
    publish("adxl345", buf);
}

void mqtt_publish_sr501(uint8_t detected)
{
    char buf[BUF_SIZE];
    snprintf(buf, sizeof(buf),
        "{\"ts\":%ld,\"detected\":%d}", time(NULL), detected);
    publish("sr501", buf);
}

void mqtt_publish_sr04(float dist_cm)
{
    char buf[BUF_SIZE];
    snprintf(buf, sizeof(buf),
        "{\"ts\":%ld,\"dist_cm\":%.1f}", time(NULL), dist_cm);
    publish("sr04", buf);
}

void mqtt_publish_light(uint16_t lux)
{
    char buf[BUF_SIZE];
    snprintf(buf, sizeof(buf),
        "{\"ts\":%ld,\"lux\":%u}", time(NULL), lux);
    publish("light", buf);
}

void mqtt_publish_comfort(float heat_index, uint8_t level)
{
    char buf[BUF_SIZE];
    snprintf(buf, sizeof(buf),
        "{\"ts\":%ld,\"heat_index\":%.1f,\"level\":%d}",
        time(NULL), heat_index, level);
    publish("comfort", buf);
}

void mqtt_publish_anomaly(uint8_t type, float magnitude)
{
    char buf[BUF_SIZE];
    snprintf(buf, sizeof(buf),
        "{\"ts\":%ld,\"type\":%d,\"magnitude\":%.3f}",
        time(NULL), type, magnitude);
    publish("anomaly", buf);
}

#endif /* MQTT_ENABLED */
