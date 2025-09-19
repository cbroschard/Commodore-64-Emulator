// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/MLMonitor.h"

MLMonitor::MLMonitor() :
    comp(nullptr),
    running(false)
{
    // Register all commands
    registerCommand(std::make_unique<AssembleCommand>());
    registerCommand(std::make_unique<BreakpointCommand>());
    registerCommand(std::make_unique<CartridgeCommand>());
    registerCommand(std::make_unique<CIACommand>());
    registerCommand(std::make_unique<DisassembleCommand>());
    registerCommand(std::make_unique<ExportDisassemblyCommand>());
    registerCommand(std::make_unique<GoCommand>());
    registerCommand(std::make_unique<JamCommand>());
    registerCommand(std::make_unique<MemoryDumpCommand>());
    registerCommand(std::make_unique<MemoryEditCommand>());
    registerCommand(std::make_unique<MemoryEditDirectCommand>());
    registerCommand(std::make_unique<NextCommand>());
    registerCommand(std::make_unique<PLACommand>());
    registerCommand(std::make_unique<RegisterDumpCommand>());
    registerCommand(std::make_unique<ResetCommand>());
    registerCommand(std::make_unique<SIDCommand>());
    registerCommand(std::make_unique<StepCommand>());
    registerCommand(std::make_unique<VICCommand>());
    registerCommand(std::make_unique<WatchCommand>());
}

MLMonitor::~MLMonitor() = default;

void MLMonitor::attachComputerInstance(Computer* comp)
{
    if (!comp)
    {
        throw std::runtime_error("MLMonitor: Computer not attached!");
    }
    this->comp = comp;
}

void MLMonitor::enter()
{
    running = true;
    std::string line;
    while (running)
    {
        std::cout << "monitor> ";
        if (!std::getline(std::cin, line)) break;
        handleCommand(line);
    }
}

void MLMonitor::clearBreakpoint(uint16_t bp)
{
    auto record = breakpoints.find(bp);
    if (record != breakpoints.end())
    {
        breakpoints.erase(record);
    }
}

void MLMonitor::listBreakpoints() const
{
    int index = 0;
    for (auto list : breakpoints)
    {
        std::cout << "[" << index << "]" << "  $" << std::hex << std::setw(4) << std::setfill('0') << list << std::endl;
        index++;
    }
}

void MLMonitor::addWatch(uint16_t address)
{
    uint8_t value = comp->readRAM(address);
    watchpoints[address] = value;
    std::cout << "Watchpoint set at $" << std::hex << std::setw(4) << std::setfill('0') << address
              << " (initial value = $" << std::setw(2) << static_cast<int>(value) << ")\n";
}

void MLMonitor::clearWatch(uint16_t address)
{
    if (watchpoints.erase(address))
    {
        std::cout << "Watchpoint cleared at $" << std::hex << std::setw(4) << std::setfill('0') << address << "\n";
    }
    else
    {
        std::cout << "No watchpoint found at $" << std::hex << std::setw(4) << std::setfill('0') << address << "\n";
    }
}

void MLMonitor::clearAllWatches()
{
    for (auto it = watchpoints.begin(); it != watchpoints.end(); )
    {
        uint16_t address = it->first;
        std::cout << "Watchpoint cleared at $"
                  << std::hex << std::setw(4) << std::setfill('0')
                  << address << "\n";

        it = watchpoints.erase(it); // erase returns next valid iterator
    }

    std::cout << "All watchpoints cleared.\n";
}

void MLMonitor::listWatches() const
{
    int index = 0;
    for (const auto& [address, value] : watchpoints)
    {
        std::cout << "[" << index << "]  $" << std::hex << std::setw(4) << std::setfill('0') << address
                  << " (last value=$" << std::setw(2) << static_cast<int>(value) << ")\n";
        index++;
    }
}

bool MLMonitor::checkWatch(uint16_t address, uint8_t newVal)
{
    auto it = watchpoints.find(address);
    if (it != watchpoints.end())
    {
        if (it->second != newVal)
        {
            uint8_t oldVal = it->second;
            it->second = newVal;

            std::cout << ">>> Watchpoint hit at $" << std::hex << std::setw(4) << std::setfill('0') << address
                      << ": old=$" << std::setw(2) << static_cast<int>(oldVal)
                      << " new=$" << std::setw(2) << static_cast<int>(newVal) << "\n";
            return true; // signal to break execution
        }
    }
    return false;
}

bool MLMonitor::isRasterWaitLoop(uint16_t pc, uint8_t& targetRaster)
{
    uint8_t opcode = comp->getOpCode(pc);

    // Case 1: CMP #imm / BNE (or BEQ) after LDA $D012
    if (opcode == 0xD0 || opcode == 0xF0)  // BNE or BEQ
    {
        uint8_t ldaOp = comp->getOpCode(pc - 5);
        uint8_t ldaLo = comp->getOpCode(pc - 4);
        uint8_t ldaHi = comp->getOpCode(pc - 3);
        uint8_t cmpOp = comp->getOpCode(pc - 2);
        uint8_t imm   = comp->getOpCode(pc - 1);

        if (ldaOp == 0xAD && ldaLo == 0x12 && ldaHi == 0xD0 && cmpOp == 0xC9)
        {
            targetRaster = imm;
            return true;
        }
    }

    // Case 2: CPY $D012 / BNE (or BEQ) after LDY #imm
    if (opcode == 0xD0 || opcode == 0xF0)  // BNE or BEQ
    {
        uint8_t ldyOp = comp->getOpCode(pc - 3);
        uint8_t imm   = comp->getOpCode(pc - 2);
        uint8_t cpyOp = comp->getOpCode(pc - 1);
        uint8_t cpyLo = comp->getOpCode(pc - 0); // careful: overlaps PC

        // Actually: LDY #imm (A0 xx), CPY $D012 (CC 12 D0), BNE/BEQ
        if (ldyOp == 0xA0 && cpyOp == 0xCC && cpyLo == 0x12)
        {
            uint8_t cpyHi = comp->getOpCode(pc + 1); // hi byte after $12
            if (cpyHi == 0xD0)
            {
                targetRaster = imm;
                return true;
            }
        }
    }

    return false;
}

void MLMonitor::handleCommand(const std::string& line)
{
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;

    // Normalize to lowercase
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

    if (cmd == "exit" || cmd == "q" || cmd == "quit")
    {
        running = false;
        return;
    }

    if (cmd == "help" || cmd == "h" || cmd == "?")
    {
        std::map<std::string, std::vector<std::string>> grouped;

        for (const auto& kv : commands)
        {
            grouped[kv.second->category()].push_back(kv.second->shortHelp());
        }

        std::cout << "Available commands:\n";
        for (auto& [cat, cmds] : grouped)
        {
            std::cout << "  " << cat << ":\n";
            for (auto& line : cmds)
                std::cout << "    " << line << "\n";
        }
        return;
    }

    std::vector<std::string> args;
    args.push_back(cmd);
    std::string tok;
    while (iss >> tok) args.push_back(tok);

    auto it = commands.find(cmd);
    if (it != commands.end())
    {
        it->second->execute(*this, args);
    }
    else
    {
        std::cout << "Unknown command: " << cmd << "\n";
    }
}

void MLMonitor::registerCommand(std::unique_ptr<MonitorCommand> cmd)
{
    commands[cmd->name()] = std::move(cmd);
}
