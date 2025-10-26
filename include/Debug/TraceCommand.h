// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef TRACECOMMAND_H
#define TRACECOMMAND_H

#include "Debug/MonitorCommand.h"
#include "Debug/TraceManager.h"

class TraceCommand : public MonitorCommand
{
    public:
        TraceCommand();
        virtual ~TraceCommand();

        inline void attachTraceManagerInstance(TraceManager* traceMgr) { this->traceMgr = traceMgr; }

        int order() const override;

        std::string name() const override;
        std::string category() const override;
        std::string shortHelp() const override;
        std::string help() const override;

        void execute(MLMonitor& mon, const std::vector<std::string>& args) override;

    protected:

    private:

        TraceManager* traceMgr;

        // Helper
        static std::string joinArgs(const std::vector<std::string>& a, size_t start);
};

#endif // TRACECOMMAND_H
