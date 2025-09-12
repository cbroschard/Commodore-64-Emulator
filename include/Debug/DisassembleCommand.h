// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef DISASSEMBLECOMMAND_H
#define DISASSEMBLECOMMAND_H

#include "Debug/CommandUtils.h"
#include "6502/Disassembler.h"
#include "Debug/MonitorCommand.h"

class DisassembleCommand : public MonitorCommand
{
    public:
        DisassembleCommand();
        virtual ~DisassembleCommand();

        int order() const override;

        std::string name() const override;
        std::string category() const override;
        std::string shortHelp() const override;
        std::string help() const override;

        void execute(MLMonitor& mon, const std::vector<std::string>& args) override;

    protected:

    private:
        // Keep track of last PC disassembled
        uint16_t lastPC;
        bool hasLastPC;
};

#endif // DISASSEMBLECOMMAND_H
