Private libraries, one folder per module. The platform-independent ones (config,
log, led, time, ...) build for both the `TZT-ESP32-Lite` and `native`
environments and are tested from `test/test_<module>/`. Arduino and ESP-IDF
glue lives in `lib/esp32/`, the HTTP pages and API in `lib/web/`; both are
firmware only.
