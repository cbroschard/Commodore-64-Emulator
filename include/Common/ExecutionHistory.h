// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef EXECUTIONHISTORY_H_INCLUDED
#define EXECUTIONHISTORY_H_INCLUDED

#include <cstddef>
#include <cstdint>
#include "Common/RingBuffer.h"

struct ExecutionHistoryEntry
{
    // Address and raw instruction bytes before execution.
    uint16_t pc = 0;

    uint8_t opcode = 0;
    uint8_t operand1 = 0;
    uint8_t operand2 = 0;

    // CPU state before execution.
    uint8_t a = 0;
    uint8_t x = 0;
    uint8_t y = 0;
    uint8_t sp = 0;
    uint8_t sr = 0;

    // Timing state at the instruction boundary.
    uint32_t totalCycles = 0;

    int rasterLine = 0;
    int rasterDot = 0;
};

class ExecutionHistory
{
    public:
        explicit ExecutionHistory(std::size_t capacity = 4096) :
            entries(capacity),
            enabled(true)
        {
        }

        void record(const ExecutionHistoryEntry& entry)
        {
            if (!enabled)
                return;

            entries.push(entry);
        }

        void clear() noexcept
        {
            entries.clear();
        }

        void setEnabled(bool value) noexcept
        {
            enabled = value;
        }

        [[nodiscard]] bool isEnabled() const noexcept
        {
            return enabled;
        }

        [[nodiscard]] bool empty() const noexcept
        {
            return entries.empty();
        }

        [[nodiscard]] std::size_t size() const noexcept
        {
            return entries.size();
        }

        [[nodiscard]] std::size_t capacity() const noexcept
        {
            return entries.capacity();
        }

        [[nodiscard]] const ExecutionHistoryEntry& at(
            std::size_t index) const
        {
            return entries.at(index);
        }

        [[nodiscard]] const ExecutionHistoryEntry& operator[](
            std::size_t index) const noexcept
        {
            return entries[index];
        }

    private:
        RingBuffer<ExecutionHistoryEntry> entries;
        bool enabled;
};

#endif // EXECUTIONHISTORY_H_INCLUDED
