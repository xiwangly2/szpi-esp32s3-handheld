# Notices

This repository contains an ESP-IDF application and a small PHP image API helper.

Third-party components are not vendored except for the local FatFs override used
by the firmware. Dependencies downloaded by ESP-IDF Component Manager keep their
own upstream licenses.

The firmware enables optional exFAT support in FatFs. Check the FatFs/exFAT
license terms for your own commercial distribution plan before shipping a
product.

The generated Chinese font source at
`firmware/handheld/main/assets/font_alipuhui20.c` should be treated as a
generated asset. For a polished public release, replacing it with a smaller
subset or external TF-card font is recommended.
