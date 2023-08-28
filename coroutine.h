/**
 * @file coroutine.h
 * @author souma
 * @brief 多线程有栈式协程池，请不要用-O0编译否则会产生coredump
 * @version 0.1
 * @date 2023-06-06
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#pragma once
#include <functional>
#include <memory>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <signal.h>
#include <pthread.h>
#include <condition_variable>
#include <unistd.h>

namespace comm {
    class Future;
    class CoroutinePool;
    class Coroutine;
    struct CoroutinePoolCtx;
    struct CoroutineTaskCtx;


    struct CoroutinePoolCtx {
        std::vector<std::shared_ptr<Coroutine>> m_vecCoroutine;
        std::shared_ptr<Coroutine> m_pMainCoroutine;
        uint32_t m_uCursor;
        uint32_t m_uWorkCnt;
    };

    struct CoroutineTaskCtx {
        std::function<void()> m_userFunc;
        std::shared_ptr<Future> m_pFuture;
    };

    // class ArraySyncQueue start
    template <class T>
    class ArraySyncQueue {
    public:
        ArraySyncQueue(uint32_t uCapacity, uint32_t uSleepUs = 100, uint32_t uRetryTimes = 3);
        bool Push(T *pObj);
        T* Pop();
        inline bool IsFull() const { return m_uPushCursor == m_uPopCursor - 1 || (m_uPopCursor == 0 && m_uPushCursor == m_vecQueue.size() - 1); }
        bool IsEmpty() const { return m_uPopCursor == m_uPushCursor; }

        ~ArraySyncQueue();

    private:
        uint32_t GetNextCursor(uint32_t uCursor);
    private:
        std::vector<T*> m_vecQueue;
        uint32_t m_uPushCursor = 0;
        uint32_t m_uPopCursor = 0;
        uint32_t m_uSleepUs;
        uint32_t m_uRetryTimes;
    };
    // class ArraySyncQueue end

    // class Coroutine start
    class Coroutine {
    public:
        
        friend class CoroutinePool;

        /**
         * @brief 调用该函数将执行流交给其他协程，仅在协程池环境下有效
         * 
         * @return true:协程切换成功, false:不在协程池环境中运行
         */
        static bool CoYield();
        
        Coroutine(const Coroutine &) = delete;
        Coroutine(Coroutine &&) = delete;
        Coroutine & operator=(const Coroutine &) = delete;
        Coroutine & operator=(Coroutine &&) = delete;

    private:
        // 4096是预留给库使用的栈内存大小，后者是留给用户使用的栈内存大小
        constexpr static uint32_t CO_STACK_SIZE = 4096 + 65535; 

        Coroutine();

        /**
         * @brief 当前协程是否绑定了任务
         *
         * @return true:是
         */
        inline bool HasTask() const { return m_pTaskCtx != nullptr; }

        /**
         * @brief 两个协程切换，从pPrev切换到pNext
         */
        static void Switch(Coroutine *pPrev, Coroutine *pNext);

        /**
         * @brief 将控制流转给同线程的其他协程
         */
        void Yield();

        /**
         * @brief 这个是给main协程用的
         */
        void Register();

        /**
         * @brief 这个是给执行用户任务的协程用的
         */
        void Register(std::shared_ptr<CoroutineTaskCtx> pTaskCtx);

        /**
         * @return CoroutinePoolCtx& 当前线程的协程上下文
         */
        static CoroutinePoolCtx & GetCtx();

        /**
         * @brief 让当前线程的cursor往后移，轮询协程
         */
        static void MoveCursor();
        
        /**
         * @brief 协程包一层的函数
         */
        static void DoWork(Coroutine *pThis);

        /**
         * 
         * @return void* 获得自建rsp地址
         */
        void* GetRsp();

        /**
         * 保存寄存器的值到m_pStack中
         */
        void SaveReg();

    private:
        void* m_pRegister[14];
        char m_pStack[CO_STACK_SIZE];
        std::shared_ptr<CoroutineTaskCtx> m_pTaskCtx;
    };
    // class Coroutine end

    // class CoroutinePool start
    class CoroutinePool {
    public:
        friend class Coroutine;
        /**
         * @brief 建立一个多线程协程池，即创建uThreadCnt个线程，每个线程含有uCoroutineCnt个协程
                  调用Run开始运行，调用Stop或直接析构结束
         * @param uThreadCnt 线程数，小于1则为1
         * @param uCoroutineCnt 每个线程的协程数，小于1则为1
         * @param uJobQueueSize 总任务队列大小，小于1则为1
         */
        CoroutinePool(uint32_t uThreadCnt, uint32_t uCoroutineCnt, uint32_t uJobQueueSize = 1024000);

        /**
         * @brief 线程安全，可重入
         * @return true:正常
         */
        bool Run();

        /**
         * @brief 停止协程池 (会先保证池中任务完成再停止)，线程安全可重入
         * 
         */
        void Stop();

        /**
         * @param userFunc 用户函数
         * @return std::shared_ptr<Future>  nullptr:协程池队列满了，提交不了
         */
        std::shared_ptr<Future> Submit(const std::function<void()> &userFunc);

        ~CoroutinePool();
        CoroutinePool(const CoroutinePool &) = delete;
        CoroutinePool(CoroutinePool &&) = delete;
        CoroutinePool & operator=(const CoroutinePool &) = delete;
        CoroutinePool & operator=(CoroutinePool &&) = delete;

    private:
        static void LoopWork(CoroutinePool &oPool);

    private:
        bool m_bStarted;
        uint32_t m_uThreadCnt;
        uint32_t m_uRoutineCnt;
        ArraySyncQueue<CoroutineTaskCtx> m_queueJob;
        std::vector<std::shared_ptr<std::thread>> m_vecThread;
        std::mutex m_oMutex;
        std::condition_variable m_oCondition;
    };
    // class CoroutinePool end

    // class Future start
    class Future {
    public:
        /**
        * @brief 阻塞获得结果
        * 
        * @param uTimeoutMs 超时时间
        * @return true:成功， false:超时
        */
        bool Get(uint32_t uTimeoutMs = -1);

        /**
        * @brief 设置状态为完成
        */
        void SetFinished();

        Future();

        Future(const Future&) = delete;
        Future(Future&&) = delete;

        Future & operator=(const Future&) = delete;
        Future & operator=(Future&&) = delete;

    private:
        std::mutex m_oMutex;
        std::condition_variable m_oCondition;
        bool m_bFinished;
    };
    // class Future end
}