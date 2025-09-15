#ifndef CIACOMMAND_H
#define CIACOMMAND_H

#include "Debug/MonitorCommand.h"

class CIACommand : public MonitorCommand
{
    public:
        CIACommand();
        virtual ~CIACommand();

        int order() const override;

        std::string name() const override;
        std::string category() const override;
        std::string shortHelp() const override;
        std::string help() const override;

        void execute(MLMonitor& mon, const std::vector<std::string>& args) override;

    protected:

    private:
};

#endif // CIACOMMAND_H
