/**
 * @file example.cpp
 * @author souma
 * @brief 使用协程池的示例，编译命令如下
 * g++ example.cpp coroutine.cpp -lpthread -O3
 * @version 0.1
 * @date 2023-06-06
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#include <iostream>
#include <array>
#include "coroutine.h"

using namespace std;
using namespace comm;

void func(const string &sTaskName, uint32_t uWaitSeconds) {
    printf("[%ld] [%s start], wait seconds[%u]\n", time(nullptr), sTaskName.c_str(), uWaitSeconds);
    time_t iStartSec = time(nullptr);
    // 默认可用65535字节的栈内存，具体可看CO_STACK_SIZE
    uint32_t uArrSize = 65535/4;
    int arr[uArrSize];
    while (time(nullptr) - iStartSec < uWaitSeconds) {
        // 操作栈内存
        for (int i = 0; i < uArrSize; ++i) {
            arr[i] = i;
        }

        // 切换控制流
        printf("[%ld] [%s] -> [协程池]\n", time(nullptr), sTaskName.c_str());
        usleep(100);
        Coroutine::CoYield(); // 只需这一个函数即可切换控制流
        printf("[%ld] [协程池] -> [%s]\n", time(nullptr), sTaskName.c_str());
    }

    // 检查栈内存是否正确
    for (int i = 0; i < uArrSize; ++i) {
        if (arr[i] != i) {
            printf("栈内存错误\n");
            exit(-1);
        }
    }
    printf("[%ld] [%s end], expect_timecost[%d], real_timecost[%ld]\n", time(nullptr), sTaskName.c_str(), uWaitSeconds, time(nullptr) - iStartSec);
}

int main() {
    // 如果想当线程池用，可以令第一个参数为线程数，第二个参数为1。
    // 在该场景下，使用小线程大协程不仅CPU消耗低，整体耗时也很低，可以自行测试。
    CoroutinePool oPool(2, 300);
    oPool.Run();

    time_t iStartTime = time(nullptr);
    const int iTaskCnt = 400;
    vector<shared_ptr<Future>> vecFuture;
    for (int i = 0; i < iTaskCnt; ++i) {
        // 模拟GetData中的Wait环节, 1 ~ 5秒等待
        shared_ptr<Future> pFuture = oPool.Submit([i](){func("Task" + to_string(i), random() % 5 + 1);});
        if (pFuture != nullptr) {
            vecFuture.emplace_back(pFuture);
        }
    }
    
    // 阻塞等待所有Task完成
    for (auto it = vecFuture.begin(); it != vecFuture.end(); ++it) {
        (*it)->Get();
    }

    printf("demo's finished, time cost[%ld]\n", time(nullptr) - iStartTime);
    return 0;
}