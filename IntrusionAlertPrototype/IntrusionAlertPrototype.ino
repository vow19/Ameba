/*
 Example guide:
 https://ameba-doc-arduino-sdk.readthedocs-hosted.com/en/latest/ameba_pro2/amb82-mini/Example_Guides/Neural%20Network/Object%20Detection.html

 NN Model Selection
 Select Neural Network(NN) task and models using modelSelect(nntask, objdetmodel, facedetmodel, facerecogmodel).
 Replace with NA_MODEL if they are not necessary for your selected NN Task.

 NN task
 =======
 OBJECT_DETECTION/ FACE_DETECTION/ FACE_RECOGNITION

 Models
 =======
 YOLOv3 model         DEFAULT_YOLOV3TINY   / CUSTOMIZED_YOLOV3TINY
 YOLOv4 model         DEFAULT_YOLOV4TINY   / CUSTOMIZED_YOLOV4TINY
 YOLOv7 model         DEFAULT_YOLOV7TINY   / CUSTOMIZED_YOLOV7TINY
 SCRFD model          DEFAULT_SCRFD        / CUSTOMIZED_SCRFD
 MobileFaceNet model  DEFAULT_MOBILEFACENET/ CUSTOMIZED_MOBILEFACENET
 No model             NA_MODEL
 */

#include "WiFi.h"
#include "StreamIO.h"
#include "VideoStream.h"
#include "RTSP.h"
#include "NNObjectDetection.h"
#include "VideoStreamOverlay.h"
#include "WiFiCredentials.h"
#include "DetectionSnapshot.h"
#include "IntrusionLogic.h"
#include "WebPreview.h"
#include <math.h>
extern "C" {
#include "FreeRTOS.h"
#include "queue.h"
}
// 整合版：模型結果 → ROI 狀態機 → LED、OSD、Serial 與 HTTP。
constexpr int PERSON_CLASS = 0;
constexpr int MIN_SCORE = AlertConfig::MIN_SCORE;
constexpr uint32_t STALE_MS = 3000;
constexpr uint32_t SERIAL_MS = 1000;
QueueHandle_t resultQueue = nullptr;
DetectionSnapshot latest = {};
bool haveResult = false;
IntrusionLogic alert;
bool webReady = false;
uint32_t lastSerialMs = 0;
uint32_t lastUrlMs = 0;

#define CHANNEL   0
#define CHANNELNN 3

// Lower resolution for NN processing
#define NNWIDTH  576
#define NNHEIGHT 320

// 降低觀看負載；16:9 與原觀看串流相同，實際框對齊仍需測試。
// 此版 SDK 的預設解析度建構函式未處理 VIDEO_WVGA，改用寬高建構函式。
VideoSetting config(640, 360, 15, VIDEO_H264, 0);
VideoSetting configNN(NNWIDTH, NNHEIGHT, 10, VIDEO_RGB, 0);
// 獨立 JPEG 通道，網頁只取快照；保留原 RTSP 通道。
VideoSetting configJPEG(640, 360, 15, VIDEO_JPEG, 1);
NNObjectDetection ObjDet;
RTSP rtsp;
StreamIO videoStreamer(1, 1);
StreamIO videoStreamerNN(1, 1);

int status = WL_IDLE_STATUS;

IPAddress ip;
int rtsp_portnum;

void setup()
{
    Serial.begin(115200);
    Serial.println("INTRUSION_WEB_V1" );
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, AlertConfig::LED_ACTIVE_HIGH ? LOW : HIGH);
    Serial.print("VIDEO_CONFIG: "); Serial.print(config.width());
    Serial.print('x'); Serial.print(config.height());
    Serial.print(" fps="); Serial.println(config.fps());
    if (config.width() != 640 || config.height() != 360) {
        Serial.println("ERROR: unexpected video dimensions; camera not started");
        while (true) { delay(1000); }
    }
    resultQueue = xQueueCreate(1, sizeof(DetectionSnapshot));
    if (resultQueue == nullptr) {
        Serial.println("ERROR: result queue allocation failed");
        while (true) { delay(1000); }
    }

    // attempt to connect to Wifi network:
    while (status != WL_CONNECTED) {
        Serial.print("Attempting to connect to WPA SSID: ");
        Serial.println(ssid);
        status = WiFi.begin(ssid, pass);

        // wait 2 seconds for connection:
        delay(2000);
    }
    ip = WiFi.localIP();

    // Configure camera video channels with video format information
    // Adjust the bitrate based on your WiFi network quality
    config.setBitrate(768 * 1024); // 觀看串流約 768 Kibit/s，待實測調整。
    Camera.configVideoChannel(CHANNEL, config);
    Camera.configVideoChannel(CHANNELNN, configNN);
    if (AlertConfig::ENABLE_WEB) Camera.configVideoChannel(1, configJPEG);
    Camera.videoInit();

    // Configure RTSP with corresponding video format information
    rtsp.configVideo(config);
    rtsp.begin();
    rtsp_portnum = rtsp.getPort();

    // Configure object detection with corresponding video format information
    // Select Neural Network(NN) task and models
    ObjDet.configVideo(configNN);
    ObjDet.modelSelect(OBJECT_DETECTION, DEFAULT_YOLOV4TINY, NA_MODEL, NA_MODEL);
    ObjDet.configThreshold(MIN_SCORE / 100.0f, 0.3f);
    ObjDet.setResultCallback(onDetection);
    ObjDet.begin();

    // Configure StreamIO object to stream data from video channel to RTSP
    videoStreamer.registerInput(Camera.getStream(CHANNEL));
    videoStreamer.registerOutput(rtsp);
    if (videoStreamer.begin() != 0) {
        Serial.println("StreamIO link start failed");
    }

    // Start data stream from video channel
    Camera.channelBegin(CHANNEL);
    if (AlertConfig::ENABLE_WEB) Camera.channelBegin(1);

    // Configure StreamIO object to stream data from RGB video channel to object detection
    videoStreamerNN.registerInput(Camera.getStream(CHANNELNN));
    videoStreamerNN.setStackSize();
    videoStreamerNN.setTaskPriority();
    videoStreamerNN.registerOutput(ObjDet);
    if (videoStreamerNN.begin() != 0) {
        Serial.println("StreamIO link start failed");
    }

    // Start video channel for NN
    Camera.channelBegin(CHANNELNN);

    // Start OSD drawing on RTSP video channel
    OSD.configVideo(CHANNEL, config);
    OSD.configTextSize(CHANNEL, 8, 16);
    if (AlertConfig::ENABLE_WEB) {
        OSD.configVideo(1, configJPEG);
        OSD.configTextSize(1, 8, 16);
    }
    OSD.begin();
    if (AlertConfig::ENABLE_WEB) {
        webReady = startWebPreview();
        if (!webReady) Serial.println("WEB_INIT_FAILED: RTSP and local detection remain available");
    }
    printConnection();
}

// SDK 在推論任務呼叫：僅複製結果，不繪圖、不進行網路傳送。
void onDetection(std::vector<ObjectDetectionResult> results)
{
    static uint32_t sequence = 0;
    DetectionSnapshot frame = {};
    frame.sequence = ++sequence;
    frame.capturedMs = millis();
    for (auto &item : results) {
        if (item.type() != PERSON_CLASS || item.score() < MIN_SCORE) { continue; }
        float x0 = item.xMin(), y0 = item.yMin();
        float x1 = item.xMax(), y1 = item.yMax();
        if (!isfinite(x0) || !isfinite(y0) || !isfinite(x1) || !isfinite(y1)) { continue; }
        x0 = fmaxf(0.0f, fminf(1.0f, x0)); y0 = fmaxf(0.0f, fminf(1.0f, y0));
        x1 = fmaxf(0.0f, fminf(1.0f, x1)); y1 = fmaxf(0.0f, fminf(1.0f, y1));
        if (x1 <= x0 || y1 <= y0) { continue; }
        PersonBox box = {x0, y0, x1, y1, item.score()};
        // 在截斷顯示框之前，對所有有效人物做 OR 判斷。
        frame.anyInside = frame.anyInside || footInRoi(box);
        frame.anyInsideExpanded = frame.anyInsideExpanded || footInRoi(box, AlertConfig::EXIT_MARGIN);
        if (frame.count == MAX_PERSONS) { frame.truncated = true; continue; }
        frame.people[frame.count++] = {x0, y0, x1, y1, item.score()};
    }
    // 零人物結果也要傳送，才能清除上一張影像的框。
    xQueueOverwrite(resultQueue, &frame);
}

void printConnection()
{
    Serial.print("WiFi status: "); Serial.println(WiFi.status());
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("RTSP URL: rtsp://"); Serial.print(WiFi.localIP());
        Serial.print(":"); Serial.println(rtsp.getPort());
        if (webReady) { Serial.print("WEB URL: http://"); Serial.println(WiFi.localIP()); }
    } else {
        Serial.println("NETWORK_DISCONNECTED: local detection continues; web task retries WiFi.");
    }
}

// 與官方範例一致，以輸出寬高換算正規化座標，限制在有效像素內。
int pixel(float value, int size) {
    int p = (int)(value * size);
    if (p < 0) return 0;
    return p >= size ? size - 1 : p;
}
void drawOverlay(int channel, uint32_t now) {
    const int w = 640, h = 360;
    const bool stale = alert.stale(now);
    OSD.createBitmap(channel);
    uint32_t zoneColor = stale ? OSD_COLOR_GRAY : (alert.warning ? OSD_COLOR_RED : OSD_COLOR_YELLOW);
    OSD.drawRect(channel, pixel(AlertConfig::ROI_LEFT,w), pixel(AlertConfig::ROI_TOP,h),
        pixel(AlertConfig::ROI_RIGHT,w), pixel(AlertConfig::ROI_BOTTOM,h), 3, zoneColor);
    OSD.drawText(channel, pixel(AlertConfig::ROI_LEFT,w), pixel(AlertConfig::ROI_TOP,h) - 18, "DANGER ZONE", zoneColor);
    if (alert.warning) {
        // 薄框是釋放用的擴張邊界，不是另一個危險區。
        OSD.drawRect(channel, pixel(AlertConfig::ROI_LEFT-AlertConfig::EXIT_MARGIN,w), pixel(AlertConfig::ROI_TOP-AlertConfig::EXIT_MARGIN,h),
            pixel(AlertConfig::ROI_RIGHT+AlertConfig::EXIT_MARGIN,w), pixel(AlertConfig::ROI_BOTTOM+AlertConfig::EXIT_MARGIN,h), 1, OSD_COLOR_ORANGE);
    }
    if (!stale) {
        for (unsigned i = 0; i < latest.count; ++i) {
            const PersonBox &b = latest.people[i];
            int x = pixel(b.xmin,w), y = pixel(b.ymin,h);
            OSD.drawRect(channel, x,y,pixel(b.xmax,w),pixel(b.ymax,h),2,OSD_COLOR_WHITE);
            OSD.drawPoint(channel, pixel((b.xmin+b.xmax)*0.5f,w),pixel(b.ymax,h),5,
                footInRoi(b) ? OSD_COLOR_RED : OSD_COLOR_GREEN);
            char text[24]; snprintf(text,sizeof(text),"person %d",b.score);
            OSD.drawText(channel, x < 500 ? x : 500, y > 48 ? y-16 : 32, text, OSD_COLOR_CYAN);
        }
    }
    char title[72];
    snprintf(title,sizeof(title),"%s  EVENTS %lu  PEOPLE %d%s",alert.state(now),(unsigned long)alert.events,
        stale ? -1 : (int)latest.count, latest.truncated ? "+" : "");
    OSD.drawText(channel,4,0,title,stale ? OSD_COLOR_YELLOW : (alert.warning ? OSD_COLOR_RED : OSD_COLOR_GREEN));
    if (stale && alert.warning) OSD.drawText(channel,4,16,"PREVIOUS WARNING HELD",OSD_COLOR_RED);
    OSD.update(channel);
}
void loop()
{
    DetectionSnapshot incoming;
    const bool updated = xQueueReceive(resultQueue, &incoming, 0) == pdTRUE;
    const uint32_t now = millis();
    bool event = false;
    alert.tick(now);
    if (updated && uint32_t(now - incoming.capturedMs) <= AlertConfig::STALE_MS) {
        latest = incoming; haveResult = true;
        event = alert.update(latest.sequence,latest.capturedMs,latest.anyInside,latest.anyInsideExpanded);
    }
    // 未有新結果時不增加幀數；STALE 時保留已觸發警告，不能假裝已離開。
    digitalWrite(LED_BUILTIN, (alert.warning == AlertConfig::LED_ACTIVE_HIGH) ? HIGH : LOW);
    if (event) {
        Serial.print("EVENT_ENTER count="); Serial.print(alert.events);
        Serial.print(" ms="); Serial.println(now);
    }
    static uint32_t lastDraw = 0, lastPublish = 0;
    if (uint32_t(now-lastDraw) >= 200) {
        lastDraw=now; drawOverlay(CHANNEL,now);
        if (AlertConfig::ENABLE_WEB) drawOverlay(1,now);
    }
    if (uint32_t(now-lastPublish) >= 100) {
        lastPublish=now;
        WebStatus s = {};
        snprintf(s.state,sizeof(s.state),"%s",alert.state(now));
        s.publishedMs=now; s.frameMs=alert.lastFrameMs; s.sequence=alert.lastSequence;
        s.events=alert.events; s.persons=alert.stale(now) ? -1 : latest.count;
        s.haveFrame=alert.haveFrame; s.warning=alert.warning; s.ledOn=alert.warning; s.truncated=latest.truncated;
        publishWebStatus(s);
    }
    if (uint32_t(now-lastSerialMs) >= SERIAL_MS) {
        lastSerialMs=now;
        Serial.print("STATE="); Serial.print(alert.state(now));
        Serial.print(" events="); Serial.print(alert.events);
        Serial.print(" seq="); Serial.print(alert.lastSequence);
        Serial.print(" inside="); Serial.print(latest.anyInside ? 1 : 0);
        Serial.print(" persons="); Serial.println(alert.stale(now) ? -1 : (int)latest.count);
    }
    if (uint32_t(now-lastUrlMs) >= 10000) { lastUrlMs=now; printConnection(); }
    delay(10);
}