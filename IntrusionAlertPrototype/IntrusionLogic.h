#pragma once
#include <stdint.h>
#include <math.h>
#include "AlertConfig.h"
#include "DetectionSnapshot.h"

// 與 Arduino 硬體分離；同一份程式由桌面測試直接執行。
inline bool qualifyingBox(const PersonBox &b) {
    return b.score >= AlertConfig::MIN_SCORE && b.score <= 100 &&
        isfinite(b.xmin) && isfinite(b.ymin) && isfinite(b.xmax) && isfinite(b.ymax) &&
        b.xmin >= 0 && b.ymin >= 0 && b.xmax <= 1 && b.ymax <= 1 &&
        b.xmax > b.xmin && b.ymax > b.ymin;
}
inline bool footInRoi(const PersonBox &b, float margin = 0) {
    if (!qualifyingBox(b)) return false;
    const float x = (b.xmin + b.xmax) * 0.5f, y = b.ymax;
    return x >= AlertConfig::ROI_LEFT - margin && x <= AlertConfig::ROI_RIGHT + margin &&
        y >= AlertConfig::ROI_TOP - margin && y <= AlertConfig::ROI_BOTTOM + margin;
}

class IntrusionLogic {
public:
    bool warning = false;
    uint32_t events = 0;
    bool haveFrame = false;
    uint32_t lastFrameMs = 0;
    uint32_t lastSequence = 0;

    bool stale(uint32_t now) const {
        return !haveFrame || uint32_t(now - lastFrameMs) > AlertConfig::STALE_MS;
    }
    const char *state(uint32_t now) const {
        if (stale(now)) return haveFrame ? "STALE" : "WAITING";
        return warning ? "WARNING" : "SAFE";
    }
    // 無新結果不累計影格，也不把失聯當成離開。保留既有 warning 鎖存。
    void tick(uint32_t now) {
        if (haveFrame && uint32_t(now - lastFrameMs) > AlertConfig::MAX_FRAME_GAP_MS) {
            enterFrames = exitFrames = 0;
        }
    }
    bool update(uint32_t sequence, uint32_t timestamp, bool inside, bool inExpandedRoi) {
        if (haveFrame && int32_t(sequence - lastSequence) <= 0) return false;
        if (haveFrame && uint32_t(timestamp - lastFrameMs) > AlertConfig::MAX_FRAME_GAP_MS) {
            enterFrames = exitFrames = 0;
        }
        lastSequence = sequence; lastFrameMs = timestamp; haveFrame = true;
        if (!warning) {
            exitFrames = 0;
            if (!inside) { enterFrames = 0; return false; }
            if (enterFrames == 0) enterSince = timestamp;
            if (enterFrames < AlertConfig::ENTER_FRAMES) ++enterFrames;
            if (enterFrames >= AlertConfig::ENTER_FRAMES &&
                uint32_t(timestamp - enterSince) >= AlertConfig::ENTER_MS &&
                (!everTriggered || uint32_t(timestamp - lastEventMs) >= AlertConfig::COOLDOWN_MS)) {
                warning = true; enterFrames = 0; everTriggered = true; lastEventMs = timestamp;
                if (events != UINT32_MAX) ++events;
                return true;
            }
        } else {
            enterFrames = 0;
            if (inExpandedRoi) { exitFrames = 0; return false; }
            if (exitFrames == 0) exitSince = timestamp;
            if (exitFrames < AlertConfig::EXIT_FRAMES) ++exitFrames;
            if (exitFrames >= AlertConfig::EXIT_FRAMES &&
                uint32_t(timestamp - exitSince) >= AlertConfig::EXIT_MS) {
                warning = false; exitFrames = 0;
            }
        }
        return false;
    }
private:
    unsigned enterFrames = 0, exitFrames = 0;
    uint32_t enterSince = 0, exitSince = 0, lastEventMs = 0;
    bool everTriggered = false;
};
