// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
#ifndef LOGGING_H
#define LOGGING_H

#include <fstream>
#include <string>
#include <string_view>
#include <vector>
#include <ctime>
#include <array>
#include <cstdint>

class Logging
{
public:
    enum class LogLevel : uint8_t { DEBUG = 0, INFO = 1, WARNING = 2, ERROR = 3 };

    explicit Logging(const std::string& filename,
                     size_t flushThresholdBytes = 64 * 1024,
                     size_t fileBufferBytes     = 256 * 1024);

    ~Logging() noexcept;

    // Treat this as "minimum level to log"
    void setLogLevel(LogLevel minLevel) noexcept;

    // Compatibility: logs at INFO
    void WriteLog(const std::string& message) { WriteLog(LogLevel::INFO, message); }

    // Fast path
    void WriteLog(LogLevel level, std::string_view message);

    // Forces buffered data to the stream + flush()
    void flush() noexcept;

    void enableTimestamps(bool enabled) noexcept { timestampsEnabled = enabled; }

private:
    void appendTimestamp();
    static constexpr std::array<std::string_view, 4> LevelTags = {
        "[DEBUG]", "[INFO]", "[WARNING]", "[ERROR]"
    };

    LogLevel minLevel = LogLevel::INFO;

    std::ofstream logfile;

    // Bigger OS/stdio buffer (optional but helps)
    std::vector<char> fileIoBuffer;

    // One big accumulation buffer
    std::string outBuffer;
    size_t flushThresholdBytes;

    // Timestamp cache (updates once/sec)
    bool timestampsEnabled = true;
    std::time_t cachedSec = 0;
    char cachedTimestamp[32] = {0}; // e.g. "[2025-12-13 21:03:59]"
};

#endif // LOGGING_H
