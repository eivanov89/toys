/*
    Coroutine demo implementing TPC-C benchmark simulation.
    See README.md for detailed description.
*/

#include <algorithm>
#include <atomic>
#include <chrono>
#include <coroutine>
#include <deque>
#include <future>
#include <iostream>
#include <mutex>
#include <optional>
#include <ranges>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>
#include <x86intrin.h>

#include "future.h"
#include "spinlock.h"

//-----------------------------------------------------------------------------

constexpr static std::chrono::duration ClockResolution = std::chrono::microseconds(50);
constexpr static std::chrono::duration BusyWaitThreshold = std::chrono::milliseconds(1);

// Workers execute "tasks": each task sleeps and then performs NumberOfAsyncRequestsPerTask requests.
// With these constants, any task should take 1 ms, so that a worker should do 1000 tasks per second.
constexpr size_t NumberOfAsyncRequestsPerTask = 4;
constexpr static std::chrono::duration AsyncRequestDuration = std::chrono::microseconds(200);
constexpr static std::chrono::duration WorkerSleepBeforeFirstRequest = std::chrono::microseconds(200);

//-----------------------------------------------------------------------------

// Thread local storage for thread ID
thread_local size_t CurrentThreadId = 0;

//-----------------------------------------------------------------------------

// Our timer thread, which is used to handle async sleep requests

class TTimerThread {
private:
    static uint64_t Rdtsc() {
        return __rdtsc();
    }

    void BusyWait(std::chrono::microseconds duration) {
        if (duration <= std::chrono::microseconds::zero()) {
            return;
        }

        uint64_t start = Rdtsc();
        uint64_t target = start + (duration.count() * RdtscPerMs / 1000);

        while (Rdtsc() < target) {
            _mm_pause();  // CPU yield instruction
        }
    }

    void Run() {
        bool isEmpty = false;
        std::vector<TTimer> expiredTimers;
        expiredTimers.reserve(1000);
        while (!StopToken.stop_requested() || !isEmpty) {
            auto now = Clock::now();
            auto maxNextWakeupDeadline = now + ClockResolution;

            std::vector<TTimer> newTimers;
            newTimers.reserve(1000);
            {
                TSpinLock::TGuard guard(SpinLock);
                if (!UnsortedTimers.empty()) {
                    std::swap(newTimers, UnsortedTimers);
                }
            }

            if (!newTimers.empty()) {
                for (auto& timer: newTimers) {
                    Timers.push_back(std::move(timer));
                }
                std::ranges::make_heap(Timers, TTimer::DeadlineCompare);
            }

            while (!Timers.empty() && Timers.front().Deadline <= now) {
                std::ranges::pop_heap(Timers, TTimer::DeadlineCompare);
                expiredTimers.push_back(std::move(Timers.back()));
                Timers.pop_back();
            }

            // Process expired timers outside the lock
            for (auto& timer: expiredTimers) {
                timer.Promise.SetValue();
            }
            expiredTimers.clear();

            isEmpty = Timers.empty();
            if (!isEmpty) {
                auto nextDeadline = Timers.front().Deadline;
                maxNextWakeupDeadline = std::min(maxNextWakeupDeadline, nextDeadline);
            }

            std::chrono::microseconds sleepTime = std::chrono::duration_cast<std::chrono::microseconds>(maxNextWakeupDeadline - now);
            if (sleepTime < BusyWaitThreshold) {
                BusyWait(sleepTime);
            } else {
                std::this_thread::sleep_for(sleepTime);
            }
        }
    }

public:
    TTimerThread(std::stop_token st)
        : StopToken(st)
    {
    }

    TTimerThread(TTimerThread&& other) noexcept
        : StopToken(std::move(other.StopToken))
        , Thread(std::move(other.Thread))
        , Timers(std::move(other.Timers))
        , RdtscPerMs(other.RdtscPerMs)
    {}

    TTimerThread& operator=(TTimerThread&& other) noexcept {
        if (this != &other) {
            StopToken = std::move(other.StopToken);
            Thread = std::move(other.Thread);
            Timers = std::move(other.Timers);
            RdtscPerMs = other.RdtscPerMs;
        }
        return *this;
    }

    TTimerThread(const TTimerThread&) = delete;
    TTimerThread& operator=(const TTimerThread&) = delete;

    void Start(uint64_t rdtscPerMs) {
        RdtscPerMs = rdtscPerMs;
        Thread = std::jthread([this]() {
            Run();
        });
    }

    void Join() {
        if (Thread.joinable()) {
            Thread.join();
        }
    }

    TFuture<void> AsyncSleep(const std::chrono::microseconds delta) {
        // Assume that when stopped, there will be no new requests to sleep
        auto deadline = Clock::now() + delta;

        TSpinLock::TGuard guard(SpinLock);
        auto& timer = UnsortedTimers.emplace_back(deadline);
        auto future = timer.Promise.get_future();

        return future;
    }

private:
    using Clock = std::chrono::steady_clock;

    struct TTimer {
        TTimer() = default;
        explicit TTimer(Clock::time_point deadline)
            : Deadline(deadline)
        {}

        static bool DeadlineCompare(const TTimer& lhs, const TTimer& rhs) {
            return lhs.Deadline > rhs.Deadline;
        }

        Clock::time_point Deadline;
        TPromise<void> Promise;
    };

    std::stop_token StopToken;
    std::jthread Thread;
    std::vector<TTimer> UnsortedTimers;
    std::vector<TTimer> Timers;
    TSpinLock SpinLock;
    uint64_t RdtscPerMs = 0;  // Calibrated rdtsc cycles per millisecond
};

class TTimerManager {
public:
    static TTimerManager& GetInstance() {
        static TTimerManager instance;
        return instance;
    }

    void Start(size_t numThreads) {
        if (Started) {
            return;
        }

        CalibrateRdtsc();
        Threads.reserve(numThreads);
        for (size_t i = 0; i < numThreads; ++i) {
            Threads.emplace_back(StopSource.get_token());
            Threads.back().Start(RdtscPerMs);
        }
        Started = true;
    }

    void Stop() {
        if (!Started) {
            return;
        }
        StopSource.request_stop();
        for (auto& thread : Threads) {
            thread.Join();
        }
        Started = false;
    }

    TFuture<void> AsyncSleep(const std::chrono::microseconds delta) {
        if (!Started) {
            throw std::runtime_error("TimerManager not started");
        }
        return Threads[CurrentThreadId % Threads.size()].AsyncSleep(delta);
    }

    std::stop_source& GetStopSource() {
        return StopSource;
    }

private:
    TTimerManager() = default;
    ~TTimerManager() {
        Stop();
    }

    static uint64_t Rdtsc() {
        return __rdtsc();
    }

    void CalibrateRdtsc() {
        constexpr int NumSamples = 100;
        std::vector<uint64_t> samples;
        samples.reserve(NumSamples);

        // Collect samples
        for (int i = 0; i < NumSamples; ++i) {
            auto start = Rdtsc();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            auto end = Rdtsc();
            samples.push_back(end - start);
        }

        // Calculate median
        std::sort(samples.begin(), samples.end());
        RdtscPerMs = samples[NumSamples / 2];
    }

    std::stop_source StopSource;
    std::vector<TTimerThread> Threads;
    uint64_t RdtscPerMs = 0;
    bool Started = false;
};

//-----------------------------------------------------------------------------

// Simulate async request, e.g., network, local I/O, or DB operation

TFuture<long> DoAsyncRequest() {
    static std::atomic<long> Value = 0;

    auto sleepFuture = TTimerManager::GetInstance().AsyncSleep(AsyncRequestDuration);
    TPromise<long> resultPromise;
    auto resultFuture = resultPromise.get_future();

    sleepFuture.Subscribe([value = &Value, promise = std::move(resultPromise)]() mutable {
        promise.SetValue(value->fetch_add(1));
    });

    return resultFuture;
}

//-----------------------------------------------------------------------------

// Coroutines stuff below

struct TTerminalTask {
    struct TPromiseType {
        long Value;
        std::future<long> Future;

        TTerminalTask get_return_object() {
            return TTerminalTask{std::coroutine_handle<TPromiseType>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        void return_value(long v) { Value = v; }
        void unhandled_exception() { std::terminate(); }
    };

    using TCoroHandle = std::coroutine_handle<TPromiseType>;

    TTerminalTask(std::coroutine_handle<TPromiseType> h)
        : Coro(h)
    {
    }

    // note, that default move constructors doesn't null Coro, so that we need
    // to add our own
    TTerminalTask(TTerminalTask&& other) noexcept
        : Coro(std::exchange(other.Coro, nullptr))
    {}

    TTerminalTask& operator=(TTerminalTask&& other) noexcept {
        if (this != &other) {
            if (Coro)
                Coro.destroy();
            Coro = std::exchange(other.Coro, nullptr);
        }
        return *this;
    }

    TTerminalTask(const TTerminalTask&) = delete;
    TTerminalTask& operator=(const TTerminalTask&) = delete;

    ~TTerminalTask() {
        if (Coro) {
            Coro.destroy();
        }
    }

    long Get() { return Coro.promise().Value; }

    std::coroutine_handle<TPromiseType> Coro;
};

struct TTerminal;

namespace std {
    template<>
    struct coroutine_traits<TTerminalTask, TTerminal&> {
        using promise_type = TTerminalTask::TPromiseType;
    };
}

//-----------------------------------------------------------------------------

class IReadyTaskQueue {
public:
    virtual ~IReadyTaskQueue() = default;
    virtual void TaskReady(TTerminalTask::TCoroHandle handle, size_t threadHint) = 0;
};

template <typename T>
struct TSuspendWithFuture {
    TSuspendWithFuture(TFuture<T>& future, IReadyTaskQueue& taskQueue)
        : Future(future)
        , TaskQueue(taskQueue)
        , ThreadId(CurrentThreadId)
    {}

    TSuspendWithFuture() = delete;
    TSuspendWithFuture(const TSuspendWithFuture&) = delete;
    TSuspendWithFuture& operator=(const TSuspendWithFuture&) = delete;

    bool await_ready() {
        return false;
    }

    void await_suspend(TTerminalTask::TCoroHandle handle) {
        if constexpr (std::is_void_v<T>) {
            Future.Subscribe([this, handle]() {
                TaskQueue.TaskReady(handle, ThreadId);
            });
        } else {
            Future.Subscribe([this, handle](T) {
                TaskQueue.TaskReady(handle, ThreadId);
            });
        }
    }

    T await_resume() {
        return Future.Get();
    }

    TFuture<T>& Future;
    IReadyTaskQueue& TaskQueue;
    size_t ThreadId;
};

//-----------------------------------------------------------------------------

struct alignas(64) TTerminalStats {
    size_t TasksDone = 0;
};

class TTerminal {
public:
    TTerminal(std::stop_token st, int terminalId, TTerminalStats& stats, IReadyTaskQueue& taskQueue)
        : StopToken(st)
        , Id(terminalId)
        , Stats(stats)
        , TaskQueue(taskQueue)
    {}

    TTerminalTask GetRunCoroutine() {
        while (!StopToken.stop_requested()) {
            // initial sleep
            //std::cout << "terminal goes to sleep\n";
            auto sleepFuture = TTimerManager::GetInstance().AsyncSleep(WorkerSleepBeforeFirstRequest);
            co_await TSuspendWithFuture<void>{sleepFuture, TaskQueue};
            //std::cout << "terminal goes to run requests\n";

            for (size_t i = 0; i < NumberOfAsyncRequestsPerTask && !StopToken.stop_requested(); ++i) {
                auto future = DoAsyncRequest();
                auto result = co_await TSuspendWithFuture<long>{future, TaskQueue};
                //std::cout << "Terminal " << Id << " got result " << i << result << std::endl;
            }

            ++Stats.TasksDone;
        }
        //std::cout << "terminal done\n";
        co_return 0;
    }

private:
    std::stop_token StopToken;
    int Id;
    TTerminalStats& Stats;
    IReadyTaskQueue& TaskQueue;
};

template <typename T>
class TSimpleCircularQueue {
public:
    TSimpleCircularQueue()
        : FirstEmpty(0)
        , FirstUsed(0)
        , Size(0)
    {
    }

    void Resize(size_t capacity) {
        Queue.resize(capacity);
    }

    bool TryPush(T&& item) {
        if (Size == Queue.size()) {
            return false;
        }
        Queue[FirstEmpty] = std::move(item);
        std::exchange(item, nullptr);
        FirstEmpty = (FirstEmpty + 1) % Queue.size();
        ++Size;
        return true;
    }

    bool TryPop(T& item) {
        if (Size == 0) {
            return false;
        }
        item = std::move(Queue[FirstUsed]);
        std::exchange(Queue[FirstUsed], nullptr);
        FirstUsed = (FirstUsed + 1) % Queue.size();
        --Size;
        return true;
    }

    size_t GetSize() const {
        return Size;
    }

    bool IsEmpty() const {
        return Size == 0;
    }

    bool IsFull() const {
        return Size == Queue.size();
    }

private:
    std::vector<T> Queue;
    size_t FirstEmpty;
    size_t FirstUsed;
    size_t Size;
};

class TTerminalPool : public IReadyTaskQueue {
public:
    TTerminalPool(std::stop_token st, int numTerminals, int numThreads)
        : PoolStopToken(st)
        , StartTime(std::chrono::steady_clock::now())
        , ThreadsData(numThreads)
    {
        TerminalStats.resize(numTerminals);

        size_t maxTerminalsPerThread = (numTerminals + numThreads - 1)  / numThreads;
        for (auto& threadData: ThreadsData) {
            threadData.ReadyTerminals.Resize(maxTerminalsPerThread);
        }

        Terminals.reserve(numTerminals);
        TerminalTasks.reserve(numTerminals);
        for (int i = 0; i < numTerminals; ++i) {
            auto& terminal = Terminals.emplace_back(PoolStopToken, i, TerminalStats[i], *this);
            auto& task = TerminalTasks.emplace_back(terminal.GetRunCoroutine());
            TaskReady(task.Coro, i);
        }

        // Create worker threads
        Threads.reserve(numThreads);
        for (int i = 0; i < numThreads; ++i) {
            Threads.emplace_back([this, i]() {
                CurrentThreadId = i;
                RunThread(i);
            });
        }
    }

    void TaskReady(TTerminalTask::TCoroHandle handle, size_t threadHint) override {
        auto index = threadHint % ThreadsData.size();
        auto& threadData = ThreadsData[index];

        TSpinLock::TGuard guard(threadData.ReadyTerminalsLock);
        threadData.ReadyTerminals.TryPush(std::move(handle));
        std::exchange(handle, nullptr);
    }

    void Join() {
        // make sure all coroutines done
        for (const auto& task: TerminalTasks) {
            while (task.Coro && !task.Coro.done()) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }

        // only now we can stop our threads, which execute coroutines
        OwnThreadsStopSource.request_stop();
        for (auto& thread: Threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }

        // Calculate and report statistics
        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(endTime - StartTime).count();

        size_t totalTasks = 0;
        for (const auto& stats : TerminalStats) {
            totalTasks += stats.TasksDone;
        }

        double rps = static_cast<double>(totalTasks) / duration;

        std::cout << "terminals: " << Terminals.size()
                  << ", RPS: " << rps
                << ", total tasks done: "<< totalTasks
                << ", duration: " << duration << " seconds" << std::endl;
    }

private:
    void RunThread(size_t threadId) {
        auto& threadData = ThreadsData[threadId];

        while (!OwnThreadsStopSource.stop_requested()) {
            std::optional<TTerminalTask::TCoroHandle> handle;
            {
                TSpinLock::TGuard guard(threadData.ReadyTerminalsLock);
                TTerminalTask::TCoroHandle h;
                if (threadData.ReadyTerminals.TryPop(h)) {
                    handle = std::move(h);
                    std::exchange(h, nullptr);
                }
            }

            if (handle) {
                if (!handle->done()) {
                    handle->resume();
                }
            }
        }
    }

private:
    struct alignas(64) TPerThreadData {
        TPerThreadData() = default;
        TSpinLock ReadyTerminalsLock;
        TSimpleCircularQueue<TTerminalTask::TCoroHandle> ReadyTerminals;
    };

private:
    std::stop_token PoolStopToken;
    std::chrono::steady_clock::time_point StartTime;

    std::stop_source OwnThreadsStopSource;

    std::vector<TTerminalStats> TerminalStats;
    std::vector<TTerminal> Terminals;
    std::vector<TTerminalTask> TerminalTasks;

    std::vector<std::thread> Threads;
    std::vector<TPerThreadData> ThreadsData;
};

//-----------------------------------------------------------------------------

void RunBenchmark(int numThreads, int numTerminals, int interval) {
    std::stop_source stopSource;
    TTerminalPool pool(stopSource.get_token(), numTerminals, numThreads);

    // execute
    std::this_thread::sleep_for(std::chrono::seconds(interval));

    // stop
    stopSource.request_stop();
    pool.Join();
}

int main(int argc, char* argv[]) {
    // Default values
    int maxThreads = std::thread::hardware_concurrency();  // Use number of CPUs by default
    int numTerminals = 10;
    int interval = 10;
    bool multiRun = false;
    bool threadsSpecified = false;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--max-threads" || arg == "-t") {
            threadsSpecified = true;
            if (i + 1 < argc) {
                try {
                    maxThreads = std::stoi(argv[++i]);
                    if (maxThreads < 1) {
                        std::cerr << "Number of threads must be positive" << std::endl;
                        return 1;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Invalid number of threads: " << e.what() << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Missing value for --max-threads option" << std::endl;
                return 1;
            }
        } else if (arg == "--max-terminals" || arg == "-w") {
            if (i + 1 < argc) {
                try {
                    numTerminals = std::stoi(argv[++i]);
                    if (numTerminals < 1) {
                        std::cerr << "Number of terminals must be positive" << std::endl;
                        return 1;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Invalid number of terminals: " << e.what() << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Missing value for --max-terminals option" << std::endl;
                return 1;
            }
        } else if (arg == "--interval" || arg == "-i") {
            if (i + 1 < argc) {
                try {
                    interval = std::stoi(argv[++i]);
                    if (interval < 1) {
                        std::cerr << "Interval must be positive" << std::endl;
                        return 1;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Invalid interval: " << e.what() << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Missing value for --interval option" << std::endl;
                return 1;
            }
        } else if (arg == "--multi") {
            multiRun = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                     << "Options:\n"
                     << "  -t, --max-threads N    Maximum number of threads (default: " << maxThreads << ")\n"
                     << "  -w, --max-terminals N  Maximum number of terminals (default: " << numTerminals << ")\n"
                     << "  -i, --interval N       Execution interval in seconds (default: " << interval << ")\n"
                     << "  --multi               Run benchmark with increasing number of terminals (1 to 128, x2 step)\n"
                     << "  -h, --help            Show this help message\n";
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            std::cerr << "Use --help for usage information" << std::endl;
            return 1;
        }
    }

    if (!threadsSpecified) {
        std::cout << "Using " << maxThreads << " threads\n";
    }

    int numTimerThreads = 1;
    if (maxThreads >= 16) {
        numTimerThreads = maxThreads / 4;
    }

    TTimerManager::GetInstance().Start(numTimerThreads);

    int maxTerminalThreads = std::max(1, maxThreads - numTimerThreads);
    maxTerminalThreads = std::min(maxTerminalThreads, numTerminals);

    if (multiRun) {
        for (int n = 1; n <= numTerminals; n *= 2) {
            int currentTerminalThreads = std::min(maxTerminalThreads, n);
            RunBenchmark(currentTerminalThreads, n, interval);
        }
    } else {
        RunBenchmark(maxTerminalThreads, numTerminals, interval);
    }

    // Stop timer thread and the rest
    TTimerManager::GetInstance().Stop();

    return 0;
}
