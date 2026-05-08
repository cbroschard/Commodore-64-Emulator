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

        void execute(MLMonitor& mon, const std::vector<std::string>& args) override;

    protected:

    private:
};

#endif // CPUCOMMAND_H
