#pragma once

#include <functional>
#include <thread>
#include <type_traits>
#include <utility>

namespace app {

class Application;

// Type-erased so Application can hold workers with different UserData types in one vector.
class WorkerBase {
    public:
        virtual ~WorkerBase() = default;

        virtual void run() = 0;
        virtual void join() = 0;
};

// Owns the thread it runs on; Application decides when run() is actually called.
template <typename UserData>
class Worker : public WorkerBase {
    public:
        Worker(Application& app, std::function<void(Application&, UserData&)> work, UserData data)
            : m_app(app), m_work(std::move(work)), m_data(std::move(data)) {}

        void run() override { m_thread = std::thread(m_work, std::ref(m_app), std::ref(m_data)); }
        void join() override {
            if (m_thread.joinable()) {
                m_thread.join();
            }
        }

    private:
        Application& m_app;
        std::function<void(Application&, UserData&)> m_work;
        UserData m_data;
        std::thread m_thread;
};

namespace detail {

// Extracts UserData from a work callable's second parameter.
template <typename F>
struct WorkerCallableTraits : WorkerCallableTraits<decltype(&std::decay_t<F>::operator())> {};

template <typename R, typename App, typename Data>
struct WorkerCallableTraits<R (*)(App, Data)> {
        using UserData = std::decay_t<Data>;
};

template <typename R, typename C, typename App, typename Data>
struct WorkerCallableTraits<R (C::*)(App, Data)> {
        using UserData = std::decay_t<Data>;
};

template <typename R, typename C, typename App, typename Data>
struct WorkerCallableTraits<R (C::*)(App, Data) const> {
        using UserData = std::decay_t<Data>;
};

} // namespace detail

} // namespace app
