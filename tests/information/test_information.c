#include "information_payload.h"
#include "information_setup_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    char pin[INFO_SETUP_PIN_SIZE]; info_setup_update_t update;
    const char *pair = "{\"pin\":\"123456\"}";
    assert(info_setup_parse_pin(pair, strlen(pair), pin)); assert(!strcmp(pin, "123456"));
    const char *bad_pair[] = { "{\"pin\":\"12345\"}", "{\"pin\":\"12345x\"}", "{\"pin\":123456}",
        "{\"pin\":\"123456\",\"pin\":\"654321\"}", "{\"pin\":\"123456\\u0000\"}", "{\"pin\":{\"pin\":\"123456\"}}" };
    for (size_t i = 0; i < sizeof(bad_pair)/sizeof(bad_pair[0]); i++) assert(!info_setup_parse_pin(bad_pair[i], strlen(bad_pair[i]), pin));
    const char *save = "{\"url\":\"https://example.com/info\",\"token\":\"new-token\",\"action\":\"replace\"}";
    assert(info_setup_parse_update(save, strlen(save), &update)); assert(update.action == INFO_TOKEN_REPLACE);
    const char *clear = "{\"url\":\"\",\"token\":\"\",\"action\":\"clear\"}";
    assert(info_setup_parse_update(clear, strlen(clear), &update)); assert(update.action == INFO_TOKEN_CLEAR);
    const char *bad_save[] = { "{\"url\":\"\",\"token\":\"\",\"action\":\"keep\"}",
        "{\"url\":\"http://example.com/info\",\"token\":\"abc\",\"action\":\"replace\"}",
        "{\"url\":\"https://example.com/info\",\"token\":\"\",\"action\":\"replace\"}",
        "{\"url\":\"https://example.com/info\",\"token\":\"abc\",\"action\":\"clear\"}",
        "{\"url\":\"https://example.com/info\",\"url\":\"https://evil.test/\",\"action\":\"keep\"}" };
    for (size_t i = 0; i < sizeof(bad_save)/sizeof(bad_save[0]); i++) assert(!info_setup_parse_update(bad_save[i], strlen(bad_save[i]), &update));
    assert(info_setup_secret_equal("secret", "secret")); assert(!info_setup_secret_equal("secret", "wrong"));
    assert(!info_setup_secret_equal("", ""));
    assert(info_setup_origin_valid("172.30.42.1:8080", "http://172.30.42.1:8080", "172.30.42.1:8080"));
    assert(info_setup_origin_valid("172.30.42.1:8080", NULL, "172.30.42.1:8080"));
    assert(!info_setup_origin_valid("evil.test:8080", NULL, "172.30.42.1:8080"));
    assert(!info_setup_origin_valid("172.30.42.1:8080", "https://evil.test", "172.30.42.1:8080"));
    assert(info_url_valid("https://example.com/info"));
    assert(info_url_valid(""));
    assert(!info_url_valid("http://example.com/info"));
    assert(!info_url_valid("https://user:pass@example.com/info"));
    assert(!info_url_valid("https://example.com/info?key=secret"));
    assert(!info_url_valid("https://example.com/info\r\nX: x"));
    assert(info_token_valid("read_token-123"));
    assert(!info_token_valid("token\nX"));
    info_document_t *doc = calloc(1, sizeof(*doc)); assert(doc);
    const char *good = "{\"version\":1,\"sections\":["
        "{\"id\":\"weather\",\"state\":\"ok\",\"source\":\"Open-Meteo.com\",\"updatedAt\":1000,\"items\":[{\"title\":\"26 C\",\"detail\":\"Cloudy\\nWind\"}]},"
        "{\"id\":\"rss\",\"state\":\"stale\",\"source\":\"RSS\",\"updatedAt\":900,\"items\":[]},"
        "{\"id\":\"services\",\"state\":\"error\",\"source\":\"Status\",\"updatedAt\":0,\"items\":[]}]}";
    assert(info_document_parse(good, strlen(good), doc));
    assert(doc->sections[0].count == 1);
    assert(!strcmp(doc->sections[0].items[0].detail, "Cloudy\nWind"));
    assert(!strcmp(doc->sections[1].state, "stale"));
    char json[2048]; strcpy(json, good);
    char *id = strstr(json, "services"); memcpy(id, "weather ", 8);
    assert(!info_document_parse(json, strlen(json), doc));
    strcpy(json, good); json[11] = '2'; assert(!info_document_parse(json, strlen(json), doc));
    strcpy(json, good); strcat(json, " garbage"); assert(!info_document_parse(json, strlen(json), doc));
    const char *nested = "[[[[[[[[[0]]]]]]]]]";
    assert(!info_document_parse(nested, strlen(nested), doc));
    strcpy(json, good); char *temperature = strstr(json, "26 C"); temperature[0] = (char)0xff;
    assert(!info_document_parse(json, strlen(json), doc));
    strcpy(json, good); char *stamp = strstr(json, "1000"); memcpy(stamp, "1e99", 4);
    assert(!info_document_parse(json, strlen(json), doc));
    if (argc > 1) {
        FILE *file = fopen(argv[1], "rb"); assert(file);
        char *body = calloc(1, INFO_BODY_LIMIT + 2); assert(body);
        size_t size = fread(body, 1, INFO_BODY_LIMIT + 1, file); fclose(file);
        assert(size <= INFO_BODY_LIMIT && info_document_parse(body, size, doc)); free(body);
        puts("Live service payload accepted by firmware parser");
    }
    free(doc); puts("Information payload checks passed"); return 0;
}
