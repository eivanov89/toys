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

//-----------------------------------------------------------------------------

constexpr static std::chrono::duration ClockResolution = std::chrono::microseconds(50);
constexpr static std::chrono::duration BusyWaitThreshold = std::chrono::milliseconds(1);

// Workers execute "tasks": each task sleeps and then performs NumberOfAsyncRequestsPerTask requests.
// With these constants, any task should take 1 ms, so that a worker should do 1000 tasks per second.
constexpr size_t NumberOfAsyncRequestsPerTask = 4;
constexpr static std::chrono::duration AsyncRequestDuration = std::chrono::microseconds(200);
constexpr static std::chrono::duration WorkerSleepBeforeFirstRequest = std::chrono::microseconds(200);

//-----------------------------------------------------------------------------

// Our timer thread, which is used to handle async sleep requests

class TTimerThread {
private:
    TTimerThread() = delete;

    TTimerThread(std::stop_token st)
        : StopToken(st)
    {
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
        while (!StopToken.stop_requested() || !isEmpty) {
            auto now = Clock::now();
            std::vector<TTimer> expiredTimers;

            {
                std::lock_guard<std::mutex> lock(Mutex);
                while (!Timers.empty() && Timers.front().Deadline <= now) {
                    std::ranges::pop_heap(Timers, TTimer::DeadlineCompare);
                    expiredTimers.push_back(std::move(Timers.back()));
                    Timers.pop_back();
                }
                isEmpty = Timers.empty();
            }

            // Process expired timers outside the lock
            for (auto& timer: expiredTimers) {
                timer.Promise.SetValue();
            }

            std::chrono::microseconds sleepTime = ClockResolution;
            {
                std::lock_guard<std::mutex> lock(Mutex);
                if (!Timers.empty()) {
                    auto nextDeadline = Timers.front().Deadline;
                    sleepTime = std::min(
                        std::chrono::duration_cast<std::chrono::microseconds>(nextDeadline - now),
                        ClockResolution
                    );
                }
            }

            if (sleepTime < BusyWaitThreshold) {
                BusyWait(sleepTime);
            } else {
                std::this_thread::sleep_for(sleepTime);
            }
        }
    }

public:
    void Start() {
        CalibrateRdtsc();
        Thread = std::jthread([this]() {
            Run();
        });
    }

    void Join() {
        if (Thread.joinable()) {
            Thread.join();
        }
    }

    static std::stop_source& GetGlobalStopSource() {
        return GlobalStopSource;
    }

    static TTimerThread& GetTimerThread() {
        return GlobarTimerThread;
    }

    TFuture<void> AsyncSleep(const std::chrono::microseconds delta) {
        // Assume that when stopped, there will be no new requests to sleep
        auto deadline = Clock::now() + delta;

        std::lock_guard<std::mutex> lock(Mutex);
        auto& timer = Timers.emplace_back(deadline);
        auto future = timer.Promise.get_future();
        std::ranges::push_heap(Timers, TTimer::DeadlineCompare);

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

    static std::stop_source GlobalStopSource;
    static TTimerThread GlobarTimerThread;
    std::stop_token StopToken;
    std::jthread Thread;
    std::vector<TTimer> Timers;
    std::mutex Mutex;
    uint64_t RdtscPerMs = 0;  // Calibrated rdtsc cycles per millisecond
};

std::stop_source TTimerThread::GlobalStopSource;
TTimerThread TTimerThread::GlobarTimerThread{GlobalStopSource.get_token()};

//-----------------------------------------------------------------------------

// Simulate async request, e.g., network, local I/O, or DB operation

TFuture<long> DoAsyncRequest() {
    static std::atomic<long> Value = 0;

    auto sleepFuture = TTimerThread::GetTimerThread().AsyncSleep(AsyncRequestDuration);
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

// Thread local storage for thread ID
thread_local size_t CurrentThreadId = 0;

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
            auto sleepFuture = TTimerThread::GetTimerThread().AsyncSleep(WorkerSleepBeforeFirstRequest);
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

class TTerminalPool : public IReadyTaskQueue {
public:
    TTerminalPool(std::stop_token st, int numTerminals, int numThreads)
        : PoolStopToken(st)
        , StartTime(std::chrono::steady_clock::now())
        , ThreadsData(numThreads)
    {
        TerminalStats.resize(numTerminals);

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

        std::lock_guard<std::mutex> lock(threadData.ReadyTerminalsMutex);
        threadData.ReadyTerminals.emplace_back(std::move(handle));
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
                std::lock_guard<std::mutex> lock(threadData.ReadyTerminalsMutex);
                if (!threadData.ReadyTerminals.empty()) {
                    handle = std::move(threadData.ReadyTerminals.front());
                    threadData.ReadyTerminals.pop_front();
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
        std::mutex ReadyTerminalsMutex;
        std::deque<TTerminalTask::TCoroHandle> ReadyTerminals;
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

void RunBenchmark(int maxThreads, int numTerminals, int interval) {
    int numThreads = std::min(maxThreads, numTerminals);

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

    TTimerThread::GetTimerThread().Start();

    int maxTerminalThreads = std::max(1, maxThreads - 1); // 1 thread for timer

    if (multiRun) {
        for (int n = 1; n <= numTerminals; n *= 2) {
            RunBenchmark(maxTerminalThreads, n, interval);
        }
    } else {
        RunBenchmark(maxTerminalThreads, numTerminals, interval);
    }

    // Stop timer thread and the rest
    TTimerThread::GetGlobalStopSource().request_stop();
    TTimerThread::GetTimerThread().Join();

    return 0;
}
