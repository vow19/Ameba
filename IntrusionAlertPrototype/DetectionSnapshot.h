#pragma once
#include <stdint.h>
// 固定容量快照；不將 vector 的內部指標跨任務傳遞。
constexpr unsigned MAX_PERSONS = 14;
struct PersonBox {
    float xmin, ymin, xmax, ymax;
    int score;
};
struct DetectionSnapshot {
    uint32_t sequence;
    uint32_t capturedMs;
    unsigned count;
    bool truncated;
    bool anyInside;
    bool anyInsideExpanded;
    PersonBox people[MAX_PERSONS];
};
