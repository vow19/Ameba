#include <Arduino.h>
#include "WiFi.h"
extern char ssid[];
extern char pass[];
#include "VideoStream.h"
#include "AlertConfig.h"
#include "WebPreview.h"
#include "WebPage.h"
extern "C" {
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
}
#include "lwip/sockets.h"
#include <errno.h>

namespace {
QueueHandle_t statusQueue = nullptr;
SemaphoreHandle_t jpegMutex = nullptr;
uint8_t jpegBuffer[AlertConfig::JPEG_CAPACITY];
uint8_t transmitBuffer[AlertConfig::JPEG_CAPACITY];
unsigned jpegSize = 0;
uint32_t jpegMs = 0;

void captureTask(void *) {
    for (;;) {
        uint32_t address = 0, length = 0;
        // SDK getImage 可能無限等待；此任務獨立，狀態機與 HTTP 不等它。
        Camera.getImage(1, &address, &length);
        if (address && length > 4 && length <= sizeof(jpegBuffer) &&
            xSemaphoreTake(jpegMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            memcpy(jpegBuffer, reinterpret_cast<const void *>(address), length);
            jpegSize = length; jpegMs = millis();
            xSemaphoreGive(jpegMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(AlertConfig::JPEG_PERIOD_MS));
    }
}

// 使用 SDK lwIP socket 回傳實際送出長度，正確處理 partial write。
bool sendAll(int fd, const void *data, size_t length) {
    const uint8_t *p = static_cast<const uint8_t *>(data);
    const uint32_t start = millis();
    while (length && uint32_t(millis() - start) < AlertConfig::HTTP_TIMEOUT_MS) {
        const size_t chunk = length > 1460 ? 1460 : length;
        const int n = lwip_send(fd, p, chunk, 0);
        if (n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
            vTaskDelay(pdMS_TO_TICKS(5)); continue;
        }
        if (n <= 0) return false;
        p += n; length -= static_cast<size_t>(n);
    }
    return length == 0;
}
bool header(int fd, int code, const char *type, unsigned length) {
    char buffer[256];
    const int n = snprintf(buffer, sizeof(buffer),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nCache-Control: no-store\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\n\r\n",
        code, code == 200 ? "OK" : "Unavailable", type, length);
    return n > 0 && n < (int)sizeof(buffer) && sendAll(fd, buffer, n);
}
void textResponse(int fd, int code, const char *body) {
    unsigned length = strlen(body);
    if (header(fd, code, "text/plain; charset=utf-8", length)) sendAll(fd, body, length);
}
void serve(int fd) {
    // 最多讀 1024 bytes 標頭，每個客戶端有限時；不接受上傳或設定變更。
    char request[1024] = {};
    size_t used = 0;
    const uint32_t start = millis();
    bool complete = false;
    while (used < sizeof(request) - 1 && uint32_t(millis() - start) < AlertConfig::HTTP_TIMEOUT_MS) {
        int n = lwip_recv(fd, request + used, sizeof(request) - used - 1, 0);
        if (n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
            vTaskDelay(pdMS_TO_TICKS(5)); continue;
        }
        if (n <= 0) break;
        used += n; request[used] = 0;
        if (strstr(request, "\r\n\r\n")) { complete = true; break; }
    }
    if (!complete) { textResponse(fd, 400, "Incomplete request"); return; }
    char method[8], path[128];
    if (sscanf(request, "%7s %127s", method, path) != 2 || strcmp(method, "GET")) {
        textResponse(fd, 405, "GET only"); return;
    }
    char *query = strchr(path, '?'); if (query) *query = 0;
    if (!strcmp(path, "/")) {
        if (header(fd, 200, "text/html; charset=utf-8", sizeof(WEB_PAGE) - 1)) sendAll(fd, WEB_PAGE, sizeof(WEB_PAGE) - 1);
    } else if (!strcmp(path, "/status.json")) {
        WebStatus s = {};
        if (xQueuePeek(statusQueue, &s, 0) != pdTRUE) { textResponse(fd, 503, "Waiting for detection"); return; }
        const bool stale = !s.haveFrame || uint32_t(millis() - s.frameMs) > AlertConfig::STALE_MS || uint32_t(millis() - s.publishedMs) > AlertConfig::STALE_MS;
        char body[512];
        int n = snprintf(body, sizeof(body),
            "{\"state\":\"%s\",\"events\":%lu,\"persons\":%d,\"sequence\":%lu,\"warning\":%s,\"led_on\":%s,\"truncated\":%s,\"frame_age_ms\":%lu,\"roi\":[%.3f,%.3f,%.3f,%.3f]}",
            stale ? "STALE" : s.state, (unsigned long)s.events, stale ? -1 : s.persons,
            (unsigned long)s.sequence, s.warning ? "true" : "false", s.ledOn ? "true" : "false",
            s.truncated ? "true" : "false", (unsigned long)(millis() - s.frameMs),
            AlertConfig::ROI_LEFT, AlertConfig::ROI_TOP, AlertConfig::ROI_RIGHT, AlertConfig::ROI_BOTTOM);
        if (n > 0 && n < (int)sizeof(body) && header(fd, 200, "application/json", n)) sendAll(fd, body, n);
    } else if (!strcmp(path, "/snapshot.jpg")) {
        unsigned length = 0;
        if (xSemaphoreTake(jpegMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (jpegSize && uint32_t(millis() - jpegMs) <= AlertConfig::IMAGE_STALE_MS) {
                length = jpegSize; memcpy(transmitBuffer, jpegBuffer, length);
            }
            xSemaphoreGive(jpegMutex);
        }
        if (!length) { textResponse(fd, 503, "Image not ready or stale"); return; }
        if (header(fd, 200, "image/jpeg", length)) sendAll(fd, transmitBuffer, length);
    } else textResponse(fd, 404, "Not found");
}

void httpTask(void *) {
    int listener = -1;
    uint32_t lastRetry = millis();
    for (;;) {
        if (WiFi.status() != WL_CONNECTED) {
            if (listener >= 0) { lwip_close(listener); listener = -1; }
            if (uint32_t(millis() - lastRetry) >= 10000) {
                lastRetry = millis();
                WiFi.begin(ssid, pass); // 可能等待，獨立於偵測主迴圈。
            }
            vTaskDelay(pdMS_TO_TICKS(250)); continue;
        }
        if (listener < 0) {
            listener = lwip_socket(AF_INET, SOCK_STREAM, 0);
            if (listener < 0) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
            int reuse = 1; lwip_setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
            struct sockaddr_in address = {};
            address.sin_family = AF_INET; address.sin_port = htons(80); address.sin_addr.s_addr = htonl(INADDR_ANY);
            if (lwip_bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0 || lwip_listen(listener, 2) < 0) {
                lwip_close(listener); listener = -1; vTaskDelay(pdMS_TO_TICKS(1000)); continue;
            }
            int nonblocking = 1;
            if (lwip_ioctl(listener, FIONBIO, &nonblocking) < 0) {
                lwip_close(listener); listener = -1; vTaskDelay(pdMS_TO_TICKS(1000)); continue;
            }
        }
        int fd = lwip_accept(listener, nullptr, nullptr);
        if (fd < 0) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        // 不依賴 SDK 是否啟用 SO_SNDTIMEO；非阻塞 socket 搭配應用程式期限。
        int nonblocking = 1;
        if (lwip_ioctl(fd, FIONBIO, &nonblocking) < 0) { lwip_close(fd); continue; }
        serve(fd); lwip_close(fd);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
}

bool startWebPreview() {
    statusQueue = xQueueCreate(1, sizeof(WebStatus));
    jpegMutex = xSemaphoreCreateMutex();
    if (!statusQueue || !jpegMutex) return false;
    if (xTaskCreate(captureTask, "jpeg_capture", 2048, nullptr, 1, nullptr) != pdPASS) return false;
    return xTaskCreate(httpTask, "alert_http", 4096, nullptr, 1, nullptr) == pdPASS;
}
void publishWebStatus(const WebStatus &status) {
    if (statusQueue) xQueueOverwrite(statusQueue, &status);
}
