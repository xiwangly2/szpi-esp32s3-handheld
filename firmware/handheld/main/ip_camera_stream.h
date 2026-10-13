#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IPCAM_URL_SIZE 512
#define IPCAM_BOUNDARY_SIZE 71
#define IPCAM_FRAME_LIMIT (512U * 1024U)
typedef enum { IPCAM_CONTENT_INVALID, IPCAM_CONTENT_JPEG, IPCAM_CONTENT_MJPEG } ipcam_content_t;
typedef bool (*ipcam_frame_cb)(void *context, const uint8_t *data, size_t size);
typedef struct ipcam_stream ipcam_stream_t;

bool ipcam_url_valid(const char *url);
ipcam_content_t ipcam_content_type(const char *value, char boundary[IPCAM_BOUNDARY_SIZE]);
bool ipcam_fit(uint16_t width, uint16_t height, uint16_t *out_width, uint16_t *out_height);
ipcam_stream_t *ipcam_stream_create(const char *boundary, uint8_t *buffer, size_t capacity,
                                    ipcam_frame_cb callback, void *context);
bool ipcam_stream_feed(ipcam_stream_t *stream, const void *data, size_t size);
bool ipcam_stream_finished(const ipcam_stream_t *stream);
void ipcam_stream_destroy(ipcam_stream_t *stream);
