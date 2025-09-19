#ifndef BREAKPOINTCOMMAND_H
#define BREAKPOINTCOMMAND_H

#include "Debug/MonitorCommand.h"

class BreakpointCommand : public MonitorCommand
{
    public:
        BreakpointCommand();
        virtual ~BreakpointCommand();

        int order() const override;

        std::string name() const override;
        std::string category() const override;
        std::string shortHelp() const override;
        std::string help() const override;

        void execute(MLMonitor& mon, const std::vector<std::string>& args) override;

    protected:

    private:
};

#endif // BREAKPOINTCOMMAND_H
