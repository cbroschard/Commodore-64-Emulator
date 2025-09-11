// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include <array>
#include <atomic>
#include <cstddef>

template<std::size_t N>
class RingBuffer {
    static_assert((N & (N - 1)) == 0, "Capacity must be a power of two");

    std::array<double, N> buf{};
    std::atomic<std::size_t> head{0};     // next write position
    std::atomic<std::size_t> tail{0};     // next read position

    static constexpr std::size_t mask = N - 1;

public:
    /* producer â€” returns false if the buffer is full */
    bool push(double sample) noexcept
    {
        auto h = head.load(std::memory_order_relaxed);
        auto next = (h + 1) & mask;
        if (next == tail.load(std::memory_order_acquire))
            return false;                 // overrun
        buf[h] = sample;
        head.store(next, std::memory_order_release);
        return true;
    }

    /* consumer â€” returns false if the buffer is empty */
    bool pop(double &sample) noexcept
    {
        auto t = tail.load(std::memory_order_relaxed);
        if (t == head.load(std::memory_order_acquire))
            return false;                 // underrun
        sample = buf[t];
        tail.store((t + 1) & mask, std::memory_order_release);
        return true;
    }

    std::size_t size() const noexcept
    {
        auto h = head.load(std::memory_order_acquire);
        auto t = tail.load(std::memory_order_acquire);
        return (h + N - t) & mask;
    }

    std::size_t capacity() const noexcept { return N - 1; }
};

#endif
