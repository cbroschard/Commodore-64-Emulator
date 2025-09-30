// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef LOGCOMMAND_H
#define LOGCOMMAND_H

#include <algorithm>
#include <optional>
#include "Debug/MonitorCommand.h"

class LogCommand : public MonitorCommand
{
    public:
        LogCommand();
        virtual ~LogCommand();

        int order() const override;

        std::string category() const override;
        std::string name() const override;
        std::string shortHelp() const override;
        std::string help() const override;

        void execute(MLMonitor& mon, const std::vector<std::string>& args) override;

    protected:

    private:

        // Define and map the types
        struct Logged;
        std::vector<Logged> logStatus;

        // Helpers
        std::optional<LogSet> stringToLogSet(const std::string& name);
        std::string logSetToString(LogSet type);
        bool setLogEnabled(std::vector<Logged>& logs, LogSet type, bool enabled);
        std::string stateString(bool enabled);
};

#endif // LOGCOMMAND_H
