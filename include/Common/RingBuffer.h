// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef RINGBUFFER_H_INCLUDED
#define RINGBUFFER_H_INCLUDED

#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Common
{
    template<typename T>
    class RingBuffer
    {
        public:
            explicit RingBuffer(std::size_t capacity) :
                buffer(capacity),
                nextWriteIndex(0),
                storedCount(0)
            {
            }

            /**
             * Add an entry by copying it into the buffer.
             *
             * Once the buffer reaches capacity, the oldest entry is overwritten.
             */
            void push(const T& value)
            {
                if (buffer.empty())
                    return;

                buffer[nextWriteIndex] = value;

                advanceWriteIndex();
            }

            /**
             * Add an entry by moving it into the buffer.
             *
             * Once the buffer reaches capacity, the oldest entry is overwritten.
             */
            void push(T&& value)
            {
                if (buffer.empty())
                    return;

                buffer[nextWriteIndex] = std::move(value);

                advanceWriteIndex();
            }

            /**
             * Remove all logically stored entries.
             *
             * The allocated storage and capacity are preserved.
             */
            void clear() noexcept
            {
                nextWriteIndex = 0;
                storedCount = 0;
            }

            /**
             * Return the number of entries currently stored.
             */
            [[nodiscard]] std::size_t size() const noexcept
            {
                return storedCount;
            }

            /**
             * Return the maximum number of entries the buffer can hold.
             */
            [[nodiscard]] std::size_t capacity() const noexcept
            {
                return buffer.size();
            }

            /**
             * Return true when no entries are stored.
             */
            [[nodiscard]] bool empty() const noexcept
            {
                return storedCount == 0;
            }

            /**
             * Return true when the buffer has reached capacity.
             */
            [[nodiscard]] bool full() const noexcept
            {
                return !buffer.empty() && storedCount == buffer.size();
            }

            /**
             * Access an entry by logical index.
             *
             * Index 0 is the oldest retained entry.
             * Index size() - 1 is the newest retained entry.
             *
             * Throws std::out_of_range if the index is invalid.
             */
            [[nodiscard]] const T& at(std::size_t index) const
            {
                validateIndex(index);

                return buffer[toPhysicalIndex(index)];
            }

            /**
             * Access an entry by logical index.
             *
             * Index 0 is the oldest retained entry.
             * Index size() - 1 is the newest retained entry.
             *
             * Throws std::out_of_range if the index is invalid.
             */
            [[nodiscard]] T& at(std::size_t index)
            {
                validateIndex(index);

                return buffer[toPhysicalIndex(index)];
            }

            /**
             * Access an entry without bounds checking.
             *
             * Index 0 is the oldest retained entry.
             * Index size() - 1 is the newest retained entry.
             */
            [[nodiscard]] const T& operator[](std::size_t index) const noexcept
            {
                return buffer[toPhysicalIndex(index)];
            }

            /**
             * Access an entry without bounds checking.
             *
             * Index 0 is the oldest retained entry.
             * Index size() - 1 is the newest retained entry.
             */
            [[nodiscard]] T& operator[](std::size_t index) noexcept
            {
                return buffer[toPhysicalIndex(index)];
            }

            /**
             * Return the oldest retained entry.
             *
             * Throws std::out_of_range if the buffer is empty.
             */
            [[nodiscard]] const T& front() const
            {
                return at(0);
            }

            /**
             * Return the oldest retained entry.
             *
             * Throws std::out_of_range if the buffer is empty.
             */
            [[nodiscard]] T& front()
            {
                return at(0);
            }

            /**
             * Return the newest retained entry.
             *
             * Throws std::out_of_range if the buffer is empty.
             */
            [[nodiscard]] const T& back() const
            {
                if (empty())
                    throw std::out_of_range("RingBuffer is empty");

                return at(storedCount - 1);
            }

            /**
             * Return the newest retained entry.
             *
             * Throws std::out_of_range if the buffer is empty.
             */
            [[nodiscard]] T& back()
            {
                if (empty())
                    throw std::out_of_range("RingBuffer is empty");

                return at(storedCount - 1);
            }

        private:
            /**
             * Advance the insertion position after writing an entry.
             */
            void advanceWriteIndex() noexcept
            {
                nextWriteIndex = (nextWriteIndex + 1) % buffer.size();

                if (storedCount < buffer.size())
                    ++storedCount;
            }

            /**
             * Convert a logical oldest-to-newest index into the corresponding
             * physical vector index.
             */
            [[nodiscard]] std::size_t toPhysicalIndex(
                std::size_t logicalIndex) const noexcept
            {
                std::size_t oldestIndex = 0;

                if (full())
                    oldestIndex = nextWriteIndex;

                return (oldestIndex + logicalIndex) % buffer.size();
            }

            /**
             * Verify that a logical index refers to a stored entry.
             */
            void validateIndex(std::size_t index) const
            {
                if (index >= storedCount)
                    throw std::out_of_range("RingBuffer index out of range");
            }

            std::vector<T> buffer;

            // Physical index where the next entry will be written.
            std::size_t nextWriteIndex;

            // Number of logically stored entries.
            std::size_t storedCount;
    };
}

#endif // RINGBUFFER_H_INCLUDED
