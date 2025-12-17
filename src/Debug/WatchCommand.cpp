// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/MLMonitor.h"
#include "Debug/WatchCommand.h"

WatchCommand::WatchCommand() = default;

WatchCommand::~WatchCommand() = default;

int WatchCommand::order() const
{
    return 20;
}

std::string WatchCommand::name() const
{
    return "watch";
}

std::string WatchCommand::category() const
{
    return "Debugging";
}

std::string WatchCommand::shortHelp() const
{
    return "watch [read|write|both]      - Manage watchpoints";
}

std::string WatchCommand::help() const
{
    return R"(watch - manage watchpoints (reads, writes, or both)

Listing:
  watch                 List all watchpoints in a single table (R/W/RW)
  watch list            Same as above

Adding:
  watch <addr>          Add WRITE watch (on change) at <addr>   (back-compat)
  watch write <addr>    Add WRITE watch (same as above)
  watch read  <addr>    Add READ watch
  watch both  <addr>    Add BOTH read & write watches (alias: 'rw')

Clearing:
  watch clear                   Clear ALL watches (both kinds)
  watch clear <addr>            Clear BOTH at <addr>
  watch clear read  <addr>      Clear READ at <addr>
  watch clear write <addr>      Clear WRITE at <addr>

Examples:
  watch $DC0D read        Break whenever CIA1 ICR is READ (IRQ ack)
  watch $0001 write       Break when CPU port ($0001) changes (cassette motor)
  watch both $DC02        Watch DDRA on both reads & writes
)";
}

void WatchCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    auto parse_addr = [&](const std::string& s)->uint16_t { return parseAddress(s); };
    auto isMode = [](std::string m)
    {
        std::transform(m.begin(),m.end(),m.begin(),::tolower);
        return m=="read"||m=="r"||m=="write"||m=="w"||m=="both"||m=="rw";
    };
    auto toBits = [](std::string m)->uint8_t
    {
        std::transform(m.begin(),m.end(),m.begin(),::tolower);
        if (m=="read"||m=="r")   return 1;   // R
        if (m=="write"||m=="w")  return 2;   // W
        return 3;                            // both/rw
    };

    // help -> print usage
    if (isHelp(args[1])) { std::cout << help(); return; }

    // LIST
    if (args[1]=="list" || (args.size()==1))
    {
        std::unordered_map<uint16_t, uint8_t> modes; // 1=R, 2=W

        for (auto a : mon.getReadWatchAddresses())  modes[a] |= 1;
        for (auto a : mon.getWriteWatchAddresses()) modes[a] |= 2;

        std::vector<uint16_t> addrs;
        addrs.reserve(modes.size());
        for (auto &kv : modes) addrs.push_back(kv.first);
        std::sort(addrs.begin(), addrs.end());

        if (addrs.empty()) { std::cout << "(no watchpoints)\n"; return; }

        for (auto a : addrs)
        {
            uint8_t m = modes[a];
            std::cout << "$" << std::hex << std::uppercase
                      << std::setw(4) << std::setfill('0') << a
                      << "   " << ((m&1)?"R":"-") << ((m&2)?"W":"-") << "\n";
        }
        return;
    }

    // CLEAR
    if (args[1]=="clear")
    {
        if (args.size()==2) { mon.clearAllReadWatches(); mon.clearAllWriteWatches(); return; }

        // watch clear <addr>      => both
        if (args.size()==3 && !isMode(args[2]))
        {
            try
            {
                uint16_t a = parse_addr(args[2]);
                mon.clearReadWatch(a); mon.clearWriteWatch(a);
            }
            catch (...)
            {
                std::cout << "Error: invalid address\n";
            }
            return;
        }

        // watch clear <mode> <addr>
        if (args.size()==4 && isMode(args[2]))
        {
            try
            {
                uint16_t a = parse_addr(args[3]);
                uint8_t b = toBits(args[2]);
                if (b & 1) mon.clearReadWatch(a);
                if (b & 2) mon.clearWriteWatch(a);
            }
            catch (...)
            {
                std::cout << "Error: invalid address\n";
            }
            return;
        }

        std::cout << help(); return;
    }

    // ADD
    try
    {
        // watch <mode> <addr>
        if (isMode(args[1]))
        {
            if (args.size()<3) { std::cout << "Error: missing address\n" << help(); return; }
            uint8_t b = toBits(args[1]);
            uint16_t a = parse_addr(args[2]);
            if (b & 1) mon.addReadWatch(a);
            if (b & 2) mon.addWriteWatch(a);
            return;
        }

        // watch <addr>  (default WRITE for back-compat)
        uint16_t a = parse_addr(args[1]);
        mon.addWriteWatch(a);
    }
    catch (...)
    {
        std::cout << "Error: invalid address\n" << help();
    }
}
