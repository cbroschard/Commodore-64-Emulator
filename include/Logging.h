// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef LOGGING_H
#define LOGGING_H

#include <iostream>
#include <fstream>
#include <string>
#include <ctime>
#include <iomanip>
#include <vector>

class Logging
{
    public:
        Logging(const std::string& filename);
        virtual ~Logging();

        enum LogLevel {DEBUG, INFO, WARNING, ERROR};

        void setLogLevel(LogLevel level);
        void WriteLog(const std::string& message);
        void flush(); // Method to flush buffer for performance

    protected:

    private:

        LogLevel CurrentLogLevel = INFO;
        std::ofstream logfile;
        std::vector<std::string> logBuffer; // Buffer for logs
        size_t bufferSize;      // Number of logs before flush
};

#endif // LOGGING_H
