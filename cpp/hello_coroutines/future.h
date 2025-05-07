#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>
#include <memory>

// Default future doesn't have Apply(), which we need
// Note, that callback set in Apply() is called in a thread setting
// the value (in our case this is the timer thread)
//
// As expected, this is quick and dirty draft implementation

template <typename T>
class TSharedState {
public:
    using Callback = std::function<void(T)>;

    template <typename TT>
    void SetValue(TT&& value) {
        std::lock_guard lock(Mutex);
        Value = std::forward<TT>(value);
        IsSet = true;
        for (auto& cb : Callbacks) {
            cb(*Value);
        }

        Callbacks.clear();
        CondVar.notify_all();
    }

    void AddCallback(Callback cb) {
        std::lock_guard lock(Mutex);
        if (IsSet) {
            cb(*Value);
        } else {
            Callbacks.push_back(std::move(cb));
        }
    }

    void Wait() {
        std::unique_lock lock(Mutex);
        CondVar.wait(lock, [this] { return IsSet; });
    }

    T Get() {
        std::unique_lock lock(Mutex);
        CondVar.wait(lock, [this] { return IsSet; });
        return *Value;
    }

private:
    std::mutex Mutex;
    std::condition_variable CondVar;
    std::optional<T> Value;
    bool IsSet = false;
    std::vector<Callback> Callbacks;
};

template <>
class TSharedState<void> {
public:
    using Callback = std::function<void()>;

    void SetValue() {
        std::lock_guard lock(Mutex);
        IsSet = true;
        for (auto& cb : Callbacks) {
            cb();
        }
        Callbacks.clear();
        CondVar.notify_all();
    }

    void AddCallback(Callback cb) {
        std::lock_guard lock(Mutex);
        if (IsSet) {
            cb();
        } else {
            Callbacks.push_back(std::move(cb));
        }
    }

    void Wait() {
        std::unique_lock lock(Mutex);
        CondVar.wait(lock, [this] { return IsSet; });
    }

    void Get() {
        Wait();
    }

private:
    std::mutex Mutex;
    std::condition_variable CondVar;
    bool IsSet = false;
    std::vector<Callback> Callbacks;
};

template<typename T>
class TFuture {
public:
    using Callback = typename TSharedState<T>::Callback;

    TFuture(std::shared_ptr<TSharedState<T>> state)
        : State(state)
    {}

    void Subscribe(Callback callback) {
        if (State) {
            State->AddCallback(std::move(callback));
        }
    }

    void Wait() {
        if (State) {
            State->Wait();
        }
    }

    T Get() {
        if (State) {
            return State->Get();
        }
        return T{};
    }

private:
    std::shared_ptr<TSharedState<T>> State;
};

template<typename T>
class TPromise {
public:
    TPromise()
        : State(std::make_shared<TSharedState<T>>())
    {}

    TFuture<T> get_future() {
        return TFuture<T>(State);
    }

    template<typename TT>
    void SetValue(TT&& value) {
        State->SetValue(std::forward<TT>(value));
    }

private:
    std::shared_ptr<TSharedState<T>> State;
};

template<>
class TFuture<void> {
public:
    using Callback = typename TSharedState<void>::Callback;

    TFuture(std::shared_ptr<TSharedState<void>> state)
        : State(state)
    {}

    void Subscribe(Callback callback) {
        if (State) {
            State->AddCallback(std::move(callback));
        }
    }

    void Wait() {
        if (State) {
            State->Wait();
        }
    }

    void Get() {
        if (State) {
            State->Get();
        }
    }

private:
    std::shared_ptr<TSharedState<void>> State;
};

template<>
class TPromise<void> {
public:
    TPromise()
        : State(std::make_shared<TSharedState<void>>())
    {}

    TFuture<void> get_future() {
        return TFuture<void>(State);
    }

    void SetValue() {
        State->SetValue();
    }

private:
    std::shared_ptr<TSharedState<void>> State;
};
