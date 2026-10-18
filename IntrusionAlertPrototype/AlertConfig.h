#pragma once
#include <stdint.h>
// 正規化座標：左上 (0,0)、右下 (1,1)。邊界包含在警戒區內。
namespace AlertConfig {
constexpr float ROI_LEFT = 0.30f;
constexpr float ROI_TOP = 0.45f;
constexpr float ROI_RIGHT = 0.75f;
constexpr float ROI_BOTTOM = 0.95f;
constexpr float EXIT_MARGIN = 0.025f; // 警告期間用稍大的區域，減少邊界抖動。
constexpr int MIN_SCORE = 50;
constexpr unsigned ENTER_FRAMES = 3;
constexpr uint32_t ENTER_MS = 200;
constexpr unsigned EXIT_FRAMES = 3;
constexpr uint32_t EXIT_MS = 1500; // 低信心/短暫漏偵測視為缺席，必須持續才釋放。
constexpr uint32_t MAX_FRAME_GAP_MS = 700;
constexpr uint32_t STALE_MS = 3000;
constexpr uint32_t COOLDOWN_MS = 1000; // 入侵事件之間的最短間隔，仍須先確認離開。
constexpr uint32_t HTTP_TIMEOUT_MS = 1500;
constexpr uint32_t JPEG_PERIOD_MS = 750;
constexpr uint32_t IMAGE_STALE_MS = 3500;
constexpr unsigned JPEG_CAPACITY = 128 * 1024;
constexpr bool ENABLE_WEB = true;
constexpr bool LED_ACTIVE_HIGH = true; // 依官方 GPIO 範例，實體亮滅仍需驗證。
static_assert(ROI_LEFT >= 0 && ROI_LEFT < ROI_RIGHT && ROI_RIGHT <= 1, "ROI x invalid");
static_assert(ROI_TOP >= 0 && ROI_TOP < ROI_BOTTOM && ROI_BOTTOM <= 1, "ROI y invalid");
}
