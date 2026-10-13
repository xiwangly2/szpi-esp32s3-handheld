#include "ip_camera_stream.h"
#include "multipart_parser.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

typedef struct { unsigned count; size_t bytes; bool cancel; } collector_t;
static bool collect(void *context, const uint8_t *data, size_t size)
{
    collector_t *c = context;
    if (c->cancel) return false;
    assert(size >= 4 && data[0] == 255 && data[1] == 216);
    c->count++; c->bytes += size;
    return true;
}

static void urls(void)
{
    const char *valid[] = {"http://192.168.1.2:8080/video", "https://camera.example/snap.jpg?a=1&b=2",
                          "http://user:pass@192.168.1.2/mjpeg", "http://[::1]:8080/", "http://camera.local"};
    const char *invalid[] = {"", "rtsp://camera/stream", "file:///sdcard/a.jpg", "https:///x",
                            "http://x:0/", "http://x:99999/", "http://x/#fragment", "http://x/a b",
                            "http://x/\r\nX:evil", "//camera/video"};
    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) assert(ipcam_url_valid(valid[i]));
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) assert(!ipcam_url_valid(invalid[i]));
    char large[600]; memset(large, 'a', sizeof(large)); memcpy(large, "http://", 7); large[599] = 0;
    assert(!ipcam_url_valid(large)); assert(!ipcam_url_valid(NULL));
}

static void types_and_dimensions(void)
{
    char boundary[IPCAM_BOUNDARY_SIZE];
    assert(ipcam_content_type("image/jpeg", boundary) == IPCAM_CONTENT_JPEG);
    assert(ipcam_content_type("IMAGE/JPEG; charset=binary", boundary) == IPCAM_CONTENT_JPEG);
    assert(ipcam_content_type("Multipart/X-Mixed-Replace; boundary=\"camera-1\"", boundary) == IPCAM_CONTENT_MJPEG);
    assert(!strcmp(boundary, "camera-1"));
    assert(ipcam_content_type("multipart/x-mixed-replace ; foo=bar; boundary = --frame", boundary) == IPCAM_CONTENT_MJPEG);
    assert(!strcmp(boundary, "--frame"));
    const char *bad[] = {"text/html", "multipart/x-mixed-replace", "multipart/x-mixed-replace;boundary=",
        "multipart/x-mixed-replace;boundary=\"x", "multipart/x-mixed-replace;boundary=x;boundary=y",
        "multipart/x-mixed-replace;boundary=\"a\nb\"", "multipart/x-mixed-replace;boundary=a b"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        assert(ipcam_content_type(bad[i], boundary) == IPCAM_CONTENT_INVALID);
    uint16_t w, h;
    assert(ipcam_fit(320, 240, &w, &h) && w == 320 && h == 240);
    assert(ipcam_fit(640, 480, &w, &h) && w == 320 && h == 240);
    assert(ipcam_fit(1920, 1080, &w, &h) && w == 320 && h == 176);
    assert(ipcam_fit(480, 640, &w, &h) && w == 176 && h == 240);
    assert(ipcam_fit(2560, 1920, &w, &h) && w == 320 && h == 240);
    assert(!ipcam_fit(0, 240, &w, &h)); assert(!ipcam_fit(2561, 1920, &w, &h));
    assert(!ipcam_fit(2048, 1, &w, &h));
}

static const char body[] = "\r\n--frame\r\nContent-Type: image/jpeg\r\nContent-Length: 8\r\n\r\n"
    "\xff\xd8\0\1\2\3\xff\xd9\r\n--frame\r\nX-Ignored: yes\r\nContent-Type: image/jpeg\r\n\r\n"
    "\xff\xd8\xff\xd9\r\n--frame--\r\n";

static void split_everywhere(void)
{
    for (size_t split = 0; split < sizeof(body); split++) {
        uint8_t buffer[128]; collector_t c = {0};
        ipcam_stream_t *s = ipcam_stream_create("frame", buffer, sizeof(buffer), collect, &c);
        assert(s);
        assert(ipcam_stream_feed(s, body, split));
        assert(ipcam_stream_feed(s, body + split, sizeof(body) - 1 - split));
        assert(ipcam_stream_finished(s) && c.count == 2 && c.bytes == 12);
        ipcam_stream_destroy(s);
    }
    for (size_t chunk = 1; chunk <= 33; chunk++) {
        uint8_t buffer[128]; collector_t c = {0};
        ipcam_stream_t *s = ipcam_stream_create("frame", buffer, sizeof(buffer), collect, &c);
        for (size_t pos = 0; pos < sizeof(body) - 1;) {
            size_t size = sizeof(body) - 1 - pos; if (size > chunk) size = chunk;
            assert(ipcam_stream_feed(s, body + pos, size)); pos += size;
        }
        assert(c.count == 2 && ipcam_stream_finished(s));
        ipcam_stream_destroy(s);
    }
}

static void rejection(const char *body_data, size_t size, size_t capacity)
{
    uint8_t buffer[128]; collector_t c = {0};
    ipcam_stream_t *s = ipcam_stream_create("frame", buffer, capacity, collect, &c);
    assert(s && !ipcam_stream_feed(s, body_data, size));
    assert(!ipcam_stream_finished(s)); ipcam_stream_destroy(s);
}

static void malformed(void)
{
    const char mismatch[] = "--frame\r\nContent-Length: 7\r\n\r\n\xff\xd8\xff\xd9\r\n--frame--\r\n";
    const char overflow[] = "--frame\r\nContent-Length: 99999999999999999999999999\r\n\r\nx";
    const char wrong_type[] = "--frame\r\nContent-Type: text/html\r\n\r\n<html>\r\n--frame--\r\n";
    const char duplicate[] = "--frame\r\nContent-Length: 4\r\nContent-Length: 4\r\n\r\nx";
    const char negative[] = "--frame\r\nContent-Length: -1\r\n\r\nx";
    rejection(mismatch, sizeof(mismatch) - 1, 128);
    rejection(overflow, sizeof(overflow) - 1, 128);
    rejection(wrong_type, sizeof(wrong_type) - 1, 128);
    rejection(duplicate, sizeof(duplicate) - 1, 128);
    rejection(negative, sizeof(negative) - 1, 128);
    rejection(body, sizeof(body) - 1, 4);
    uint8_t buffer[128]; collector_t c = { .cancel = true };
    ipcam_stream_t *s = ipcam_stream_create("frame", buffer, sizeof(buffer), collect, &c);
    assert(s && !ipcam_stream_feed(s, body, sizeof(body) - 1)); ipcam_stream_destroy(s);
    c.cancel = false;
    s = ipcam_stream_create("frame", buffer, sizeof(buffer), collect, &c);
    assert(ipcam_stream_feed(s, body, 80)); assert(!ipcam_stream_finished(s)); ipcam_stream_destroy(s);
    assert(!multipart_parser_init(NULL, NULL));
    puts("IP camera URL, scaling, MIME, all split points, binary payload, bounds and cancellation passed");
}

int main(void)
{
#ifdef _MSC_VER
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    urls(); types_and_dimensions(); split_everywhere(); malformed(); return 0;
}
