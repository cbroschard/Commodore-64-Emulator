// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Logging.h"

Logging::Logging(const std::string& filename)
{
    logfile.open(filename, std::ios::app);
    if (!logfile.is_open())
    {
        std::cerr << "Unable to open log file!" << std::endl;
    }

    bufferSize = 100;
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
    catch(...){}
}

void Logging::setLogLevel(LogLevel level)
{
    CurrentLogLevel = level;
}

void Logging::WriteLog(const std::string &message)
{
    std::time_t now = std::time(nullptr);
    std::tm* localtime = std::localtime(&now);
    std::stringstream logEntry;

    logEntry << "[" << std::put_time(localtime, "%Y-%m-%d %H:%M:%S") << "]";

    switch(CurrentLogLevel)
    {
        case DEBUG:
            logEntry << "[DEBUG]";
            break;
        case INFO:
            logEntry << "[INFO]";
            break;
        case WARNING:
            logEntry << "[WARNING]";
            break;
        case ERROR:
            logEntry << "[ERROR]";
            break;
    }

    logEntry << message;

    logBuffer.push_back(logEntry.str());

    // Flush to file when buffer is full
    if (logBuffer.size() >= bufferSize)
    {
        flush();
    }
}

void Logging::flush()
{
    for (const auto& entry : logBuffer)
    {
        logfile << entry << "\n";
    }
    logBuffer.clear();
}
