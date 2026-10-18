#pragma once
#include <stdint.h>
// 以 queue 複製此結構，不讓網路任務直接讀寫狀態機。
struct WebStatus {
    char state[12];
    uint32_t publishedMs, frameMs, sequence, events;
    int persons;
    bool haveFrame, warning, ledOn, truncated;
};
bool startWebPreview();
void publishWebStatus(const WebStatus &status);
