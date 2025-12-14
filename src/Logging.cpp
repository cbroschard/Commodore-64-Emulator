// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
#include "Logging.h"

#include <iostream>   // only for startup error
#include <cstring>    // std::strlen

#if defined(_WIN32)
  #include <time.h> // localtime_s
#endif

Logging::Logging(const std::string& filename,
                 size_t flushThresholdBytes_,
                 size_t fileBufferBytes)
    : flushThresholdBytes(flushThresholdBytes_)
{
    logfile.open(filename, std::ios::app | std::ios::binary);
    if (!logfile.is_open())
    {
        // Avoid std::endl here (it flushes)
        std::cerr << "Unable to open log file!\n";
        return;
    }

    // Give the filebuf a larger buffer to reduce kernel writes
    fileIoBuffer.resize(fileBufferBytes);
    logfile.rdbuf()->pubsetbuf(fileIoBuffer.data(),
                               static_cast<std::streamsize>(fileIoBuffer.size()));

    // Reserve so we don't grow constantly
    outBuffer.reserve(flushThresholdBytes + 1024);
}

Logging::~Logging() noexcept
{
    try
    {
        if (logfile.is_open())
        {
            flush();
            logfile.close();
        }
    }
    catch (...) {}
}

void Logging::setLogLevel(LogLevel level) noexcept
{
    minLevel = level;
}

void Logging::appendTimestamp()
{
    if (!timestampsEnabled)
        return;

    const std::time_t now = std::time(nullptr);
    if (now == cachedSec && cachedTimestamp[0] != '\0')
        return;

    cachedSec = now;

    std::tm tmLocal{};
#if defined(_WIN32)
    localtime_s(&tmLocal, &now);
#else
    localtime_r(&now, &tmLocal);
#endif

    // Produces: [YYYY-mm-dd HH:MM:SS]
    std::strftime(cachedTimestamp, sizeof(cachedTimestamp),
                  "[%Y-%m-%d %H:%M:%S]", &tmLocal);
}

void Logging::WriteLog(LogLevel level, std::string_view message)
{
    if (!logfile.is_open())
        return;

    // Fast reject before doing any work
    if (static_cast<uint8_t>(level) < static_cast<uint8_t>(minLevel))
        return;

    appendTimestamp();

    // Rough reserve to avoid multiple growths on big messages
    outBuffer.reserve(outBuffer.size() + message.size() + 64);

    if (timestampsEnabled)
        outBuffer.append(cachedTimestamp);

    outBuffer.append(LevelTags[static_cast<size_t>(level)]);
    outBuffer.push_back(' ');
    outBuffer.append(message.data(), message.size());
    outBuffer.push_back('\n');

    if (outBuffer.size() >= flushThresholdBytes)
    {
        // Write buffered chunk; don't force flush() every time
        logfile.write(outBuffer.data(),
                      static_cast<std::streamsize>(outBuffer.size()));
        outBuffer.clear(); // keeps capacity
    }
}

void Logging::flush() noexcept
{
    try
    {
        if (!logfile.is_open())
            return;

        if (!outBuffer.empty())
        {
            logfile.write(outBuffer.data(),
                          static_cast<std::streamsize>(outBuffer.size()));
            outBuffer.clear();
        }

        // Explicit flush only when requested/destructor
        logfile.flush();
    }
    catch (...) {}
}
