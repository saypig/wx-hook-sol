// logic.h -- run a closure on WeChat's logic thread
#pragma once
#include <functional>

namespace wxhook {

bool runOnLogicThread(std::function<bool()> task, unsigned timeoutMs = 15000);
long getterHookHits();
void getterHookHit();
void drainTaskQueue();
unsigned long logicThreadId();

} // namespace wxhook
