#include "CPUCommand.h"

CPUCommand::CPUCommand() = default;

CPUCommand::~CPUCommand() = default;

int CPUCommand::order() const
{
    return 6;
}

std::string CPUCommand::name() const
{
    return "cpu";
}

std::string CPUCommand::category() const
{
    return "CPU/Execution";
}

std::string CPUCommand::shortHelp() const
{
    return "cpu       - Show CPU registers, IRQ status, cycles, etc.";
}

std::string CPUCommand::help() const
{
    return R"(CPU commands
 Usage:
  cpu regs              - Show CPU registers
  cpu irq               - Show IRQ/NMI timing state
  cpu cycles            - Show CPU cycle counters and current timing state
  cpu stack [count]     - Show stack contents
  cpu jam               - Show or set JAM/KIL opcode behavior
  cpu trace             - Show CPU trace configuration
)";
}

void CPUCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() < 2 || (args.size() == 2 && isHelp(args[1])))
    {
        std::cout << help();
        return;
    }
}
