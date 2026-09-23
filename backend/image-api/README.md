# Random Image API for ESP32-S3

Drop `image.php` into the directory that already contains `images/`, `bz/`, and
`background/`.

PHP 8.5.5 is fine. Resize mode needs the PHP GD extension enabled.

Copy `config.sample.php` to `config.php` and change `api_key`, or set the
`IMAGE_API_KEY` environment variable.

Old style stays available:

```text
https://example.com/image.php?key=change-me&return=json
```

ESP-optimized JSON:

```text
https://example.com/image.php?key=change-me&return=json&esp=1
```

Configurable resize:

```text
https://example.com/image.php?key=change-me&return=json&w=320&h=240&q=72&fit=crop
```

`fit` can be `crop`, `contain`, or `stretch`. Resize mode needs the PHP GD
extension enabled. Dynamic resized JPEG responses intentionally do not set
`Content-Length`: some PHP hosting stacks apply output compression, and a
manual length can make small clients wait forever for bytes that will never
arrive.

After upload, test:

```text
https://example.com/image.php?key=change-me&return=json&esp=1
```

Expected JSON fields for the ESP path:

```json
{
  "mime": "image/jpeg",
  "width": 320,
  "height": 240
}
```
