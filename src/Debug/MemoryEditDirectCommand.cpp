#include "Computer.h"
#include "Debug/MemoryEditDirectCommand.h"
#include "Debug/MLMonitor.h"

MemoryEditDirectCommand::MemoryEditDirectCommand() = default;

MemoryEditDirectCommand::~MemoryEditDirectCommand() = default;

int MemoryEditDirectCommand::order() const
{
    return 15;
}

std::string MemoryEditDirectCommand::name() const
{
    return "ed";
}

std::string MemoryEditDirectCommand::category() const
{
    return "Memory";
}

std::string MemoryEditDirectCommand::shortHelp() const
{
    return "ed        - Direct RAM edit (bypass bus/PLA; no I/O side effects)";
}

std::string MemoryEditDirectCommand::help() const
{
    return
        "ed <addr> [byte]\n"
        "  Write directly to underlying RAM at <addr>, ignoring current mapping\n"
        "  (PLA/$01, cartridges) and bypassing device side effects.\n"
        "  - Use to modify RAM hidden under ROM/I/O\n"
        "  - Does not touch VIC/SID/CIA registers or change border color, etc.\n"
        "Examples:\n"
        "  ed $A000 $01    ; write to RAM under BASIC ROM\n"
        "  ed $D020 $00    ; writes RAM under I/O, not the border register\n";
}

void MemoryEditDirectCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() < 3 || isHelp(args[1]))
    {
        std::cout << "Usage: " << help() << std::endl;
        return;
    }
    try
    {
        uint16_t address = parseAddress(args[1]);
        uint8_t value = parseAddress(args[2]);
        mon.computer()->writeRAMDirect(address, value);
    }
    catch(const std::exception& e)
    {
        std::cout << "Error: Invalid address or value. Usage: " << help() << std::endl;
    }
}
