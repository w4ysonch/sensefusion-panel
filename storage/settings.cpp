#include "settings.h"
#include "eeprom.h"
#include <optional>
#include <cstring>

static constexpr app_settings_t kDefaultSettings{
    SETTINGS_DEFAULT_THRESHOLD,  // anomaly_threshold
    0,                           // unit_fahrenheit
    0,                           // alert_muted
    80,                          // brightness
    SETTINGS_MAGIC,              // magic
};

void settings_load(app_settings_t *s)
{
    if (eeprom_read(SETTINGS_EEPROM_ADDR, s, sizeof(*s)) < 0
        || s->magic != SETTINGS_MAGIC) {
        *s = kDefaultSettings;
        settings_save(s);
    }
}

int settings_save(const app_settings_t *s)
{
    return eeprom_write(SETTINGS_EEPROM_ADDR, s, sizeof(*s));
}
