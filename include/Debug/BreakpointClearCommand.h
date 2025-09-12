// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef BREAKPOINTCLEARCOMMAND_H
#define BREAKPOINTCLEARCOMMAND_H

#include "Debug/CommandUtils.h"
#include "Debug/MonitorCommand.h"

// Forward declaration
class Computer;
class MLMonitor;

class BreakpointClearCommand : public MonitorCommand
{
    public:
        BreakpointClearCommand();
        virtual ~BreakpointClearCommand();

        std::string name() const override;
        std::string category() const override;
        std::string shortHelp() const override;
        std::string help() const override;

        void execute(MLMonitor& mon, const std::vector<std::string>& args) override;

    protected:

    private:
};

#endif // BREAKPOINTCLEARCOMMAND_H
