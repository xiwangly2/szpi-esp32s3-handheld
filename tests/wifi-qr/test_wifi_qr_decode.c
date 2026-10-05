#include "wifi_qr_payload.h"
#include "qrcodegen.h"
#include "quirc.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_image(struct quirc *decoder, const char *payload,
                        wifi_qr_parse_result_t expected, int rotation, bool mirror)
{
    uint8_t temp[qrcodegen_BUFFER_LEN_MAX], qr[qrcodegen_BUFFER_LEN_MAX];
    assert(qrcodegen_encodeText(payload, temp, qr, qrcodegen_Ecc_MEDIUM,
                               1, 10, qrcodegen_Mask_AUTO, true));
    int size = qrcodegen_getSize(qr);
    assert(size * 4 < 240 - 32);
    uint8_t *gray = quirc_begin(decoder, NULL, NULL);
    memset(gray, 255, 320 * 240);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int sx = mirror ? size - 1 - x : x, sy = y;
            for (int r = 0; r < rotation; r++) {
                int prev_x = sx;
                sx = size - 1 - sy;
                sy = prev_x;
            }
            uint8_t value = qrcodegen_getModule(qr, sx, sy) ? 0 : 255;
            for (int dy = 0; dy < 4; dy++) {
                for (int dx = 0; dx < 4; dx++) {
                    gray[((240 - size * 4) / 2 + y * 4 + dy) * 320 +
                         (320 - size * 4) / 2 + x * 4 + dx] = value;
                }
            }
        }
    }
    quirc_end(decoder);
    assert(quirc_count(decoder) == 1);
    struct quirc_code *code = calloc(1, sizeof(*code));
    struct quirc_data *data = calloc(1, sizeof(*data));
    assert(code != NULL && data != NULL);
    quirc_extract(decoder, 0, code);
    quirc_decode_error_t result = quirc_decode(code, data);
    if (result != QUIRC_SUCCESS) {
        quirc_flip(code);
        result = quirc_decode(code, data);
    }
    assert(result == QUIRC_SUCCESS);
    assert((size_t)data->payload_len == strlen(payload));
    assert(memcmp(payload, data->payload, strlen(payload)) == 0);
    wifi_qr_credentials_t credentials;
    assert(wifi_qr_parse(data->payload, (size_t)data->payload_len, &credentials) == expected);
    free(code);
    free(data);
}

int main(void)
{
    struct quirc *decoder = quirc_new();
    assert(decoder != NULL && quirc_resize(decoder, 320, 240) == 0);
    const char *payloads[] = {
        "WIFI:T:WPA;S:UnitTest;P:Test-pass123;;",
        "WIFI:T:nopass;S:\xe5\xae\xb6\xe9\x87\x8c;H:true;;",
        "https://example.com/",
    };
    for (size_t i = 0; i < sizeof(payloads) / sizeof(payloads[0]); i++) {
        for (int r = 0; r < 4; r++) {
            for (int m = 0; m < 2; m++) {
                check_image(decoder, payloads[i], i == 2 ? WIFI_QR_NOT_WIFI : WIFI_QR_OK, r, m != 0);
            }
        }
    }
    memset(quirc_begin(decoder, NULL, NULL), 255, 320 * 240);
    quirc_end(decoder);
    assert(quirc_count(decoder) == 0);
    quirc_destroy(decoder);
    puts("24 generated QR images decoded across rotations and mirroring; blank image rejected.");
    return 0;
}
