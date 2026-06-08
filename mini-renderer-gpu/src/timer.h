#pragma once
#include <chrono>

// CPU/GPU speed comparison timer
class Timer {
public:
    void start() {
        m_begin = std::chrono::high_resolution_clock::now();
    }

    double elapsedMs() const {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(now - m_begin).count();
    }

private:
    std::chrono::high_resolution_clock::time_point m_begin;
};
