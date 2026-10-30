// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef IEEE488BUSCOMMAND_H
#define IEEE488BUSCOMMAND_H

#include "Debug/MonitorCommand.h"

class IEEE488BusCommand : public MonitorCommand
{
    public:
        IEEE488BusCommand();
        ~IEEE488BusCommand();

        int order() const override;

        std::string name() const override;
        std::string category() const override;
        std::string shortHelp() const override;
        std::string help() const override;

        void execute(MLMonitor& mon, const std::vector<std::string>& args) override;
};

#endif // IEEE488BUSCOMMAND_H
