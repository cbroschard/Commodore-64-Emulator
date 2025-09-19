#ifndef MEMORYEDITDIRECTCOMMAND_H
#define MEMORYEDITDIRECTCOMMAND_H

#include "Debug/MonitorCommand.h"

class MemoryEditDirectCommand : public MonitorCommand
{
    public:
        MemoryEditDirectCommand();
        virtual ~MemoryEditDirectCommand();

        int order() const override;

        std::string name() const override;
        std::string category() const override;
        std::string shortHelp() const override;
        std::string help() const override;

        void execute(MLMonitor& mon, const std::vector<std::string>& args) override;

    protected:

    private:
};

#endif // MEMORYEDITDIRECTCOMMAND_H
