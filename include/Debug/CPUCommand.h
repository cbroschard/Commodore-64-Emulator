// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef CPUCOMMAND_H
#define CPUCOMMAND_H

#include "Debug/MonitorCommand.h"

class CPUCommand : public MonitorCommand
{
    public:
        CPUCommand();
        virtual ~CPUCommand();

        int order() const override;

        std::string name() const override;
        std::string category() const override;
        std::string shortHelp() const override;
        std::string help() const override;

        std::string jamUsage() const;

        void execute(MLMonitor& mon, const std::vector<std::string>& args) override;

    protected:

    private:
};

#endif // CPUCOMMAND_H
