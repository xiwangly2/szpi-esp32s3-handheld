#include "esp_idf_version.h"
#include "esp_log.h"
#include <stdarg.h>
#include <stdint.h>

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 5, 0)
// esp_extractor 1.0.7 ships a Log V1 binary using the newer esp_log entry point.
// Its config ABI is one 32-bit word, with the log level in bits 0..2. Format
// strings already contain the V1 timestamp/tag arguments; forward them intact.
typedef struct { uint32_t data; } extractor_log_config_t;

void esp_log(extractor_log_config_t config, const char *tag, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    esp_log_writev((esp_log_level_t)(config.data & 7U), tag, format, args);
    va_end(args);
}
#endif
