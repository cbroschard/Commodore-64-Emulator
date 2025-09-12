// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef MONITORCOMMAND_H
#define MONITORCOMMAND_H

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

// Forward declarations
class MLMonitor;

class MonitorCommand
{
    public:
        MonitorCommand();
        virtual ~MonitorCommand();

        virtual int order() const { return 100; } // default

        virtual std::string name() const = 0;
        virtual std::string category() const = 0;
        virtual std::string shortHelp() const = 0;
        virtual std::string help() const = 0;
        virtual void execute(MLMonitor& mon, const std::vector<std::string>& args) = 0;

    protected:
        uint16_t parseAddress(const std::string& s);
        bool isHelp(const std::string& s) const;

    private:
};

#endif // MONITORCOMMAND_H
