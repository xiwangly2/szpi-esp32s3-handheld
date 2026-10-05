#include "wifi_qr_payload.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static wifi_qr_credentials_t parse(const char *text, wifi_qr_parse_result_t expected)
{
    wifi_qr_credentials_t credentials;
    memset(&credentials, 0xa5, sizeof(credentials));
    assert(wifi_qr_parse((const uint8_t *)text, strlen(text), &credentials) == expected);
    if (expected != WIFI_QR_OK) {
        wifi_qr_credentials_t empty = {0};
        assert(memcmp(&credentials, &empty, sizeof(empty)) == 0);
    }
    return credentials;
}

int main(void)
{
    wifi_qr_credentials_t c = parse("WIFI:T:WPA;S:Home;P:password123;;", WIFI_QR_OK);
    assert(strcmp(c.ssid, "Home") == 0 && strcmp(c.password, "password123") == 0);
    assert(c.auth == WIFI_QR_AUTH_WPA && !c.hidden);
    c = parse("WIFI:P:pa\\;ss\\:wo\\\\rd;H:true;S:Office\\,West;T:WPA2;;", WIFI_QR_OK);
    assert(strcmp(c.ssid, "Office,West") == 0);
    assert(strcmp(c.password, "pa;ss:wo\\rd") == 0 && c.hidden);
    c = parse("WIFI:S:\xe5\xae\xb6\xe9\x87\x8c;T:nopass;P:ignored;;", WIFI_QR_OK);
    assert(strcmp(c.ssid, "\xe5\xae\xb6\xe9\x87\x8c") == 0 && c.password[0] == 0);
    c = parse("WIFI:S:Guest;;", WIFI_QR_OK);
    assert(c.auth == WIFI_QR_AUTH_OPEN);
    c = parse("WIFI:S:\"ABCD\";T:WPA;P:\"12345678\";H:false;;", WIFI_QR_OK);
    assert(strcmp(c.ssid, "ABCD") == 0 && !c.hidden);
    c = parse("WIFI:S:\\\"Cafe\\\";T:SAE;P: password ;;", WIFI_QR_OK);
    assert(strcmp(c.ssid, "\"Cafe\"") == 0 && strcmp(c.password, " password ") == 0);
    assert(c.auth == WIFI_QR_AUTH_WPA3);
    c = parse("WIFI:S:Other;T:WPA3;P:12345678;;", WIFI_QR_OK);
    assert(c.auth == WIFI_QR_AUTH_WPA3);
    c = parse("WIFI:S:Other;X:extension;T:WPA;P:12345678;", WIFI_QR_OK);
    assert(c.auth == WIFI_QR_AUTH_WPA);

    char text[1200];
    char ssid[34];
    memset(ssid, 's', 32); ssid[32] = 0;
    char psk[66];
    memset(psk, 'a', 64); psk[64] = 0;
    snprintf(text, sizeof(text), "WIFI:T:WPA;S:%s;P:%s;;", ssid, psk);
    c = parse(text, WIFI_QR_OK);
    assert(strlen(c.ssid) == 32 && strlen(c.password) == 64);
    ssid[32] = 's'; ssid[33] = 0;
    snprintf(text, sizeof(text), "WIFI:S:%s;;", ssid);
    parse(text, WIFI_QR_TOO_LONG);
    psk[0] = 'z';
    snprintf(text, sizeof(text), "WIFI:S:Home;T:WPA;P:%s;;", psk);
    parse(text, WIFI_QR_INVALID);
    psk[0] = 'a';
    snprintf(text, sizeof(text), "WIFI:S:Home;T:SAE;P:%s;;", psk);
    parse(text, WIFI_QR_INVALID);
    psk[64] = 'a'; psk[65] = 0;
    snprintf(text, sizeof(text), "WIFI:S:Home;T:WPA;P:%s;;", psk);
    parse(text, WIFI_QR_TOO_LONG);

    parse("https://example.com", WIFI_QR_NOT_WIFI);
    parse("DPP:K:key;;", WIFI_QR_NOT_WIFI);
    parse("WIFI:T:WEP;S:Home;P:12345678;;", WIFI_QR_UNSUPPORTED_AUTH);
    parse("WIFI:T:WPA2-EAP;S:Work;;", WIFI_QR_UNSUPPORTED_AUTH);
    parse("WIFI:S:Work;E:TLS;;", WIFI_QR_UNSUPPORTED_AUTH);
    parse("WIFI:S:Work;PH2:MSCHAPV2;;", WIFI_QR_UNSUPPORTED_AUTH);
    parse("WIFI:T:WPA;S:Home;P:short;;", WIFI_QR_INVALID);
    parse("WIFI:T:WPA;P:12345678;;", WIFI_QR_INVALID);
    parse("WIFI:S:;;", WIFI_QR_INVALID);
    parse("WIFI:S:Home;S:Other;;", WIFI_QR_INVALID);
    parse("WIFI:S:Home;T:WPA;T:nopass;P:12345678;;", WIFI_QR_INVALID);
    parse("WIFI:S:Home;P:12345678;P:87654321;;", WIFI_QR_INVALID);
    parse("WIFI:S:Home;H:true;H:false;;", WIFI_QR_INVALID);
    parse("WIFI:S:Home;H:maybe;;", WIFI_QR_INVALID);
    parse("WIFI:S:Home", WIFI_QR_INVALID);
    parse("WIFI:S:Home\\", WIFI_QR_INVALID);
    parse("WIFI:S:Home\\n;;", WIFI_QR_INVALID);
    parse("WIFI:S:Home;;S:Other;", WIFI_QR_INVALID);
    parse("WIFI:S:Home\npassword=wrong;;", WIFI_QR_INVALID);
    parse("WIFI:S:\"Home;;", WIFI_QR_INVALID);
    parse("WIFI:S:\"Home\"extra;;", WIFI_QR_INVALID);
    parse("WIFI:S:\xc0\xaf;;", WIFI_QR_INVALID);
    parse("WIFI:S:\xed\xa0\x80;;", WIFI_QR_INVALID);
    parse("WIFI:S:\xf4\x90\x80\x80;;", WIFI_QR_INVALID);
    parse("WIFI:S:\xe5\xae;;", WIFI_QR_INVALID);
    memset(text, 'a', sizeof(text));
    memcpy(text, "WIFI:", 5);
    assert(wifi_qr_parse((const uint8_t *)text, sizeof(text), &c) == WIFI_QR_TOO_LONG);
    const uint8_t binary[] = {'W','I','F','I',':','S',':','A',0,'B',';',';'};
    assert(wifi_qr_parse(binary, sizeof(binary), &c) == WIFI_QR_INVALID);
    assert(wifi_qr_parse(NULL, 0, &c) == WIFI_QR_NOT_WIFI);
    assert(wifi_qr_parse(binary, sizeof(binary), NULL) == WIFI_QR_INVALID);
    puts("Wi-Fi QR payload boundary and escaping tests passed.");
    return 0;
}
