#include <string.h>
#include "settings.h"
#include "eeprom.h"

static const app_settings_t DEFAULT_SETTINGS = {
    .anomaly_threshold = 0.3f,
    .unit_fahrenheit   = 0,
    .alert_muted       = 0,
    .brightness        = 80,
    .magic             = SETTINGS_MAGIC,
};

void settings_load(app_settings_t *s)
{
    if (eeprom_read(SETTINGS_EEPROM_ADDR, s, sizeof(*s)) < 0
        || s->magic != SETTINGS_MAGIC) {
        *s = DEFAULT_SETTINGS;
        settings_save(s);
    }
}

int settings_save(const app_settings_t *s)
{
    return eeprom_write(SETTINGS_EEPROM_ADDR, s, sizeof(*s));
}
