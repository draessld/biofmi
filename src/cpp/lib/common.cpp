#include "common.hpp"
#include <chrono>

namespace biofmi {

// Timer implementation
struct Timer::Impl {
    std::chrono::high_resolution_clock::time_point start_time;
    std::chrono::high_resolution_clock::time_point stop_time;
    bool running = false;
};

Timer::Timer() : impl_(std::make_unique<Impl>()) {}

Timer::~Timer() = default;

void Timer::start() {
    impl_->start_time = std::chrono::high_resolution_clock::now();
    impl_->running = true;
}

void Timer::stop() {
    impl_->stop_time = std::chrono::high_resolution_clock::now();
    impl_->running = false;
}

double Timer::elapsed_seconds() const {
    auto end = impl_->running ? std::chrono::high_resolution_clock::now() : impl_->stop_time;
    return std::chrono::duration<double>(end - impl_->start_time).count();
}

double Timer::elapsed_milliseconds() const {
    return elapsed_seconds() * 1000.0;
}

double Timer::elapsed_microseconds() const {
    return elapsed_seconds() * 1000000.0;
}

} // namespace biofmi
