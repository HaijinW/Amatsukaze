/**
* Sub process and thread utility
* Copyright (c) 2017-2019 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include "common.h"
#include <Windows.h>
#include <process.h>

#include <deque>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "StreamUtils.h"
#include "PerformanceUtil.h"

//#define SUBPROC_OUT (isErr ? stderr : stdout)
// 出力が混ざるのを防ぐため全てstderrに出力
#define SUBPROC_OUT stderr

// スレッドはstart()で開始（コンストラクタから仮想関数を呼ぶことはできないため）
// run()は派生クラスで実装されているのでrun()が終了する前に派生クラスのデストラクタが終了しないように注意！
// 安全のためjoin()が完了していない状態でThreadBaseのデストラクタに入るとエラーとする
class ThreadBase {
public:
    ThreadBase() : thread_handle_(NULL) {}
    ~ThreadBase() {
        if (thread_handle_ != NULL) {
            THROW(InvalidOperationException, "finish join() before destroy object ...");
        }
    }
    void start() {
        if (thread_handle_ != NULL) {
            THROW(InvalidOperationException, "thread already started ...");
        }
        thread_handle_ = (HANDLE)_beginthreadex(NULL, 0, thread_, this, 0, NULL);
        if (thread_handle_ == (HANDLE)-1) {
            THROW(RuntimeException, "failed to begin pump thread ...");
        }
    }
    void join() {
        if (thread_handle_ != NULL) {
            WaitForSingleObject(thread_handle_, INFINITE);
            CloseHandle(thread_handle_);
            thread_handle_ = NULL;
        }
    }
    bool isRunning() { return thread_handle_ != NULL; }

protected:
    virtual void run() = 0;

private:
    HANDLE thread_handle_;

    static unsigned __stdcall thread_(void* arg) {
        try {
            static_cast<ThreadBase*>(arg)->run();
        } catch (const Exception& e) {
            throw e;
        }
        return 0;
    }
};

template <typename T, bool PERF = false>
class DataPumpThread : private ThreadBase {
public:
    DataPumpThread(size_t maximum)
        : maximum_(maximum)
        , current_(0)
        , finished_(false)
        , error_(false) {}

    ~DataPumpThread() {
        if (isRunning()) {
            THROW(InvalidOperationException, "call join() before destroy object ...");
        }
    }

    void put(T&& data, size_t amount) {
        std::unique_lock<std::mutex> lock(critical_section_);
        if (error_) {
            THROW(RuntimeException, "DataPumpThread error");
        }
        if (finished_) {
            THROW(InvalidOperationException, "DataPumpThread is already finished");
        }
        while (current_ >= maximum_) {
            if (PERF) producer.start();
            cond_full_.wait(lock);
            if (PERF) producer.stop();
        }
        if (data_.size() == 0) {
            cond_empty_.notify_one();
        }
        data_.emplace_back(amount, std::move(data));
        current_ += amount;
    }

    void start() {
        finished_ = false;
        producer.reset();
        consumer.reset();
        ThreadBase::start();
    }

    void join() {
        {
            std::unique_lock<std::mutex> lock(critical_section_);
            finished_ = true;
            cond_empty_.notify_one();
        }
        ThreadBase::join();
    }

    bool isRunning() { return ThreadBase::isRunning(); }

    void getTotalWait(double& prod, double& cons) {
        prod = producer.getTotal();
        cons = consumer.getTotal();
    }

protected:
    virtual void OnDataReceived(T&& data) = 0;

private:
    std::mutex critical_section_;
    std::condition_variable cond_full_;
    std::condition_variable cond_empty_;

    std::deque<std::pair<size_t, T>> data_;

    size_t maximum_;
    size_t current_;

    bool finished_;
    bool error_;

    Stopwatch producer;
    Stopwatch consumer;

    virtual void run() {
        while (true) {
            T data;
            {
                std::unique_lock<std::mutex> lock(critical_section_);
                while (data_.size() == 0) {
                    // data_.size()==0でfinished_なら終了
                    if (finished_ || error_) return;
                    if (PERF) consumer.start();
                    cond_empty_.wait(lock);
                    if (PERF) consumer.stop();
                }
                auto& entry = data_.front();
                size_t newsize = current_ - entry.first;
                if ((current_ >= maximum_) && (newsize < maximum_)) {
                    cond_full_.notify_all();
                }
                current_ = newsize;
                data = std::move(entry.second);
                data_.pop_front();
            }
            if (error_ == false) {
                try {
                    OnDataReceived(std::move(data));
                } catch (Exception&) {
                    error_ = true;
                }
            }
        }
    }
};

class SubProcess {
public:
    SubProcess(const tstring& args, const bool disablePowerThrottoling = false);
    ~SubProcess();
    void write(MemoryChunk mc);
    size_t readErr(MemoryChunk mc);
    size_t readOut(MemoryChunk mc);
    void finishWrite();
    int join();
private:
    class Pipe {
    public:
        Pipe();
        ~Pipe();
        void closeRead();
        void closeWrite();
        HANDLE readHandle;
        HANDLE writeHandle;
    };

    void runSetPowerThrottling();

    PROCESS_INFORMATION pi_;
    Pipe stdErrPipe_;
    Pipe stdOutPipe_;
    Pipe stdInPipe_;
    DWORD exitCode_;
    std::thread thSetPowerThrottling_;
    HANDLE heSetPowerThrottlingAbort_;

    size_t readGeneric(MemoryChunk mc, HANDLE readHandle);
};

class EventBaseSubProcess : public SubProcess {
public:
    EventBaseSubProcess(const tstring& args, const bool disablePowerThrottoling = false);
    ~EventBaseSubProcess();
    int join();
    bool isRunning();
protected:
    virtual void onOut(bool isErr, MemoryChunk mc) = 0;

private:
    class DrainThread : public ThreadBase {
    public:
        DrainThread(EventBaseSubProcess* this_, bool isErr);
        virtual void run();
    private:
        EventBaseSubProcess* this_;
        bool isErr_;
    };

    DrainThread drainOut;
    DrainThread drainErr;

    void drain_thread(bool isErr);
};

class StdRedirectedSubProcess : public EventBaseSubProcess {
public:
    StdRedirectedSubProcess(const tstring& args, const int bufferLines = 0, const bool isUtf8 = false, const bool disablePowerThrottoling = false);

    ~StdRedirectedSubProcess();

    const std::deque<std::vector<char>>& getLastLines();

private:
    class SpStringLiner : public StringLiner {
        StdRedirectedSubProcess* pThis;
    public:
        SpStringLiner(StdRedirectedSubProcess* pThis, bool isErr);
    protected:
        bool isErr;
        virtual void OnTextLine(const uint8_t* ptr, int len, int brlen);
    };

    bool isUtf8;
    int bufferLines;
    SpStringLiner outLiner, errLiner;

    std::mutex mtx;
    std::deque<std::vector<char>> lastLines;

    void onTextLine(bool isErr, const uint8_t* ptr, int len, int brlen);

    virtual void onOut(bool isErr, MemoryChunk mc);
};

enum PROCESSOR_INFO_TAG {
    PROC_TAG_NONE = 0,
    PROC_TAG_CORE,
    PROC_TAG_L2,
    PROC_TAG_L3,
    PROC_TAG_NUMA,
    PROC_TAG_GROUP,
    PROC_TAG_COUNT
};

class CPUInfo {
    std::vector<GROUP_AFFINITY> data[PROC_TAG_COUNT];
public:
    CPUInfo();
    const GROUP_AFFINITY* GetData(PROCESSOR_INFO_TAG tag, int* count);
};

bool SetCPUAffinity(int group, uint64_t mask);
