/**
 * @file coroutine.cpp
 * @author souma
 * @brief 协程池的具体实现
 * @version 0.1
 * @date 2023-06-06
 * 
 * @copyright Copyright (c) 2023
 * 
 */

#include "coroutine.h"
#include <cstring>

using namespace std;
namespace comm {

    // class Coroutine start
    Coroutine::Coroutine() {
        m_pTaskCtx = nullptr;
    }

    void Coroutine::Register() {
        m_pTaskCtx = make_shared<CoroutineTaskCtx>();
        m_pTaskCtx->m_userFunc = [](){};
        m_pTaskCtx->m_pFuture = nullptr;
        SaveReg();
    }

    void Coroutine::Register(shared_ptr<CoroutineTaskCtx> pTaskCtx) {
        m_pTaskCtx = pTaskCtx;
        SaveReg();
    }

    inline void Coroutine::Yield() {
        Coroutine::Switch(this, Coroutine::GetCtx().m_pMainCoroutine.get());
    }

    bool Coroutine::CoYield() {
        if (GetCtx().m_vecCoroutine.size() == 0) {
            return false;
        }
        GetCtx().m_vecCoroutine[GetCtx().m_uCursor]->Yield();
        return true;
    }

    CoroutinePoolCtx & Coroutine::GetCtx() {
        thread_local CoroutinePoolCtx coroutinePoolCtx;
        return coroutinePoolCtx;
    }

    void Coroutine::MoveCursor() {
        GetCtx().m_uCursor = GetCtx().m_uCursor == GetCtx().m_vecCoroutine.size() - 1 ? 0 : GetCtx().m_uCursor + 1;
    }

    extern "C" __attribute__((noinline, weak))
    void Coroutine::Switch(Coroutine *pPrev, Coroutine *pNext) {
        // 1.保存pPrev协程的上下文, rdi和pPrev同指向
        // 2.加载pNext协程的上下文, rsi和pNext同指向
        asm volatile(R"(
            movq %rsp, %rax
            movq %rbp, 104(%rdi)
            movq %rax, 96(%rdi)
            movq %rbx, 88(%rdi)
            movq %rcx, 80(%rdi)
            movq %rdx, 72(%rdi)
            movq 0(%rax), %rax
            movq %rax, 64(%rdi)
            movq %rsi, 56(%rdi)
            movq %rdi, 48(%rdi)
            movq %r8, 40(%rdi)
            movq %r9, 32(%rdi)
            movq %r12, 24(%rdi)
            movq %r13, 16(%rdi)
            movq %r14, 8(%rdi)
            movq %r15, (%rdi)

            movq (%rsi), %r15
            movq 8(%rsi), %r14
            movq 16(%rsi), %r13
            movq 24(%rsi), %r12
            movq 32(%rsi), %r9
            movq 40(%rsi), %r8
            movq 48(%rsi), %rdi
            movq 64(%rsi), %rax
            movq 72(%rsi), %rdx
            movq 80(%rsi), %rcx
            movq 88(%rsi), %rbx
            movq 96(%rsi), %rsp
            movq 104(%rsi), %rbp
            movq 56(%rsi), %rsi
            movq %rax, (%rsp)
            xorq %rax, %rax
        )");
    }

    void Coroutine::DoWork(Coroutine *pThis) {
        pThis->m_pTaskCtx->m_userFunc();
        pThis->m_pTaskCtx->m_pFuture->SetFinished();
        pThis->m_pTaskCtx.reset();
        Coroutine::GetCtx().m_uWorkCnt--;
        pThis->Yield();
    }

    void* Coroutine::GetRsp() {
        // m_pRegister和m_pStack中间预留一个指针空间
        auto sp = std::end(m_pStack) - sizeof(void*);
        // 预定Rsp的地址保证能够整除8字节
        sp = decltype(sp)(reinterpret_cast<size_t>(sp) & (~0xF));
        return sp;
    }

    void Coroutine::SaveReg() {
        void *pStack = GetRsp();
        memset(m_pRegister, 0, sizeof m_pRegister);
        void **pRax = (void**)pStack;
        *pRax = (void*) DoWork;
        // rsp
        m_pRegister[12] = pStack;
        // rax
        m_pRegister[8] = *pRax;
        // rdi
        m_pRegister[6] = this;
    }
    // class Coroutine end

    // class CoroutinePool start
    CoroutinePool::CoroutinePool(uint32_t uThreadCnt, uint32_t uCoroutineCnt, uint32_t uJobQueueSize) : m_queueJob(uJobQueueSize) {
        m_bStarted = false;
        m_uThreadCnt = max(uThreadCnt, 1u);
        m_uRoutineCnt = max(uCoroutineCnt, 1u);
    }

    bool CoroutinePool::Run() {
        if (!__sync_bool_compare_and_swap(&m_bStarted, false, true)) {
            return false;
        }
        
        for (decltype(m_uThreadCnt) i = 0; i < m_uThreadCnt; ++i) {
            m_vecThread.emplace_back(make_shared<thread>(CoroutinePool::LoopWork, ref(*this)));   
        }
        return true;
    }

    void CoroutinePool::Stop() {
        if (!__sync_bool_compare_and_swap(&m_bStarted, true, false)) {
            return;
        }
        
        m_oCondition.notify_all();
        for (auto it = m_vecThread.begin(); it != m_vecThread.end(); ++it) {
            (*it)->join();
        }
        m_vecThread.clear();
    }

    shared_ptr<Future> CoroutinePool::Submit(const function<void()> &userFunc) {
        shared_ptr<Future> pNewFuture = make_shared<Future>();
        CoroutineTaskCtx *pTaskCtx = new CoroutineTaskCtx;
        pTaskCtx->m_pFuture = pNewFuture;
        pTaskCtx->m_userFunc = userFunc;

        if (!m_queueJob.Push(pTaskCtx)) {
            delete pTaskCtx, pTaskCtx = nullptr;
            return nullptr;
        }
        m_oCondition.notify_all();
        return pNewFuture;
    }

    CoroutinePool::~CoroutinePool() {
        Stop();
    }

    void CoroutinePool::LoopWork(CoroutinePool &oPool) {
        Coroutine::GetCtx().m_uCursor = 0;
        Coroutine::GetCtx().m_uWorkCnt = 0;
        Coroutine::GetCtx().m_pMainCoroutine = shared_ptr<Coroutine>(new Coroutine);
        Coroutine::GetCtx().m_pMainCoroutine->Register();

        Coroutine::GetCtx().m_vecCoroutine.clear();
        for (decltype(oPool.m_uRoutineCnt) i = 0; i < oPool.m_uRoutineCnt; ++i) {
            Coroutine::GetCtx().m_vecCoroutine.emplace_back(shared_ptr<Coroutine>(new Coroutine));
        }

        Coroutine *pMainCoroutine, *pCurCoroutine;
        while (oPool.m_bStarted || Coroutine::GetCtx().m_uWorkCnt > 0 || !oPool.m_queueJob.IsEmpty()) {
            
            pMainCoroutine = Coroutine::GetCtx().m_pMainCoroutine.get();
            pCurCoroutine = Coroutine::GetCtx().m_vecCoroutine[Coroutine::GetCtx().m_uCursor].get();
            
            if (pCurCoroutine->HasTask()) {
                Coroutine::Switch(pMainCoroutine, pCurCoroutine);
                Coroutine::MoveCursor();
                continue;
            }

            CoroutineTaskCtx *pTaskCtx = oPool.m_queueJob.Pop();
            if (pTaskCtx == nullptr) {
                if (Coroutine::GetCtx().m_uWorkCnt > 0) {
                    Coroutine::MoveCursor();
                    continue;
                }
                unique_lock<mutex> oLock(oPool.m_oMutex);
                oPool.m_oCondition.wait(oLock);
                continue;
            }

            pCurCoroutine->Register(shared_ptr<CoroutineTaskCtx>(pTaskCtx));
            ++Coroutine::GetCtx().m_uWorkCnt;
            Coroutine::Switch(pMainCoroutine, pCurCoroutine);
            Coroutine::MoveCursor();
        }
    }
    // class CoroutinePool end

    // class Future start
    Future::Future() {
        m_bFinished = false;
    }

    bool Future::Get(uint32_t uTimeoutMs) {
        unique_lock<mutex> oLock(m_oMutex);
        if (m_bFinished) {
            return true;
        }
        return m_oCondition.wait_for(oLock, chrono::milliseconds(uTimeoutMs)) == cv_status::no_timeout;
    }

    void Future::SetFinished() {
        {
            unique_lock<mutex> oLock(m_oMutex);
            m_bFinished = true;
        }
        m_oCondition.notify_all();
    }
    // class Future end

    // class ArraySyncQueue start
    template <class T>
    ArraySyncQueue<T>::ArraySyncQueue(uint32_t uCapacity, uint32_t uSleepUs, uint32_t uRetryTimes) {
        for (uint32_t i = 0; i < std::max(uCapacity, 1u); ++i) {
            m_vecQueue.emplace_back(nullptr);
        }
        m_uSleepUs = uSleepUs;
        m_uRetryTimes = uRetryTimes;
    }

    template <class T>
    bool ArraySyncQueue<T>::Push(T *pObj) {
        if (pObj == nullptr) {
            return false;
        }
        uint32_t uRetryTimes = 0;
        while (uRetryTimes <= m_uRetryTimes) {
            uint32_t uPushCursor = m_uPushCursor;
            if (uPushCursor == m_uPopCursor - 1 || (m_uPopCursor == 0 && uPushCursor == m_vecQueue.size() - 1)) {
                // 队列满了
                return false;
            }

            if (!__sync_bool_compare_and_swap(&m_vecQueue[uPushCursor], nullptr, pObj)) {
                uRetryTimes++;
                usleep(m_uSleepUs);
                continue;
            }

            m_uPushCursor = GetNextCursor(uPushCursor);
            return true;
        }
        // 竞争失败
        return false;
    }

    template <class T>
    T* ArraySyncQueue<T>::Pop() {
        uint32_t uRetryTimes = 0;
        while (uRetryTimes <= m_uRetryTimes) {
            uint32_t uPopCursor = m_uPopCursor;
            if (uPopCursor == m_uPushCursor) {
                return nullptr;
            }

            T* pToReturn = m_vecQueue[uPopCursor];
            if (pToReturn == nullptr || !__sync_bool_compare_and_swap(&m_vecQueue[uPopCursor], pToReturn, nullptr)) {
                usleep(m_uSleepUs);
                uRetryTimes++;
                continue;
            }
            m_uPopCursor = GetNextCursor(uPopCursor);
            return pToReturn;
        }
        return nullptr;
    }

    template <class T>
    uint32_t ArraySyncQueue<T>::GetNextCursor(uint32_t uCursor) {
        if (uCursor == m_vecQueue.size() - 1) {
            return 0;
        }
        return uCursor + 1;
    }

    template <class T>
    ArraySyncQueue<T>::~ArraySyncQueue() {
        m_uRetryTimes = -1;
        do {
            T *pObj = Pop();
            if (pObj == nullptr) {
                return;
            }
            delete pObj, pObj = nullptr;
        } while (true);
    }
    // class ArraySyncQueue end
}