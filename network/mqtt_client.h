#ifndef NETWORK_MQTT_CLIENT_H
#define NETWORK_MQTT_CLIENT_H

#include <stdint.h>

/* 主题前缀：sensefusion/<sensor> */
#define MQTT_TOPIC_PREFIX "sensefusion"

#ifdef MQTT_ENABLED

int  mqtt_init   (const char *host, int port, const char *client_id);
void mqtt_deinit (void);

void mqtt_publish_dht11   (float temp, float humi);
void mqtt_publish_adxl345 (float x, float y, float z, float mag);
void mqtt_publish_sr501   (uint8_t detected);
void mqtt_publish_sr04    (float dist_cm);
void mqtt_publish_light   (uint16_t lux);
void mqtt_publish_comfort (float heat_index, uint8_t level);
void mqtt_publish_anomaly (uint8_t type, float magnitude);

const char *mqtt_status_str(void);

#else  /* MQTT_ENABLED not set: compile-out stubs */

static inline int  mqtt_init(const char *h, int p, const char *id)
    { (void)h; (void)p; (void)id; return 0; }
static inline void mqtt_deinit(void) {}
static inline void mqtt_publish_dht11(float t, float h)
    { (void)t; (void)h; }
static inline void mqtt_publish_adxl345(float x, float y, float z, float m)
    { (void)x; (void)y; (void)z; (void)m; }
static inline void mqtt_publish_sr501(uint8_t d) { (void)d; }
static inline void mqtt_publish_sr04(float d)    { (void)d; }
static inline void mqtt_publish_light(uint16_t l){ (void)l; }
static inline void mqtt_publish_comfort(float h, uint8_t l) { (void)h; (void)l; }
static inline void mqtt_publish_anomaly(uint8_t t, float m) { (void)t; (void)m; }
static inline const char *mqtt_status_str(void) { return "未编译"; }

#endif /* MQTT_ENABLED */

#endif /* NETWORK_MQTT_CLIENT_H */
