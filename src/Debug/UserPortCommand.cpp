// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.

#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"
#include "Debug/UserPortCommand.h"
#include "Serial/RS232Device.h"

UserPortCommand::UserPortCommand() = default;

UserPortCommand::~UserPortCommand() = default;

int UserPortCommand::order() const
{
    return 20;
}

std::string UserPortCommand::name() const
{
    return "userport";
}

std::string UserPortCommand::category() const
{
    return "Peripheral/User Port";
}

std::string UserPortCommand::shortHelp() const
{
    return "userport [subcmd] - Inspect User Port state, lines, attached device, and RS-232";
}

std::string UserPortCommand::help() const
{
    return R"(userport - Inspect the C64 User Port

Usage:
    userport [subcommand]
    userport rs232 [subcommand]

Subcommands:
    status                         - Show User Port and attached device state
    device                         - Show the attached User Port device
    lines                          - Show C64-side User Port signal mapping and line state
    rs232                          - Show RS-232 adapter and serial device state
    rs232 test                     - Run RS-232 loopback self-test using $55 with no parity
    rs232 test <byte>              - Run RS-232 loopback self-test using a hex byte
    rs232 test <byte> <parity>     - Run RS-232 loopback test with none, odd, or even parity
    rs232 test multi               - Run RS-232 back-to-back multi-byte loopback test
    rs232 test formats             - Test RS-232 loopback across supported serial formats
    rs232 test flow                - Test RS-232 RTS/CTS hardware flow control
    rs232 test errors              - Test RS-232 parity and framing error handling
    help                           - Show this help text

Examples:
    userport
    userport status
    userport device
    userport lines
    userport rs232
    userport rs232 test
    userport rs232 test 55
    userport rs232 test 55 none
    userport rs232 test 55 odd
    userport rs232 test 55 even
    userport rs232 test AA even
)";
}

void UserPortCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() >= 2 && (args[1] == "help" || args[1] == "?"))
    {
        std::cout << help();
        return;
    }

    MLMonitorBackend* backend = mon.mlmonitorbackend();

    if (backend == nullptr)
    {
        std::cout << "Monitor backend is not attached.\n";
        return;
    }

    // Bare "userport" is the same as "userport status".
    if (args.size() == 1)
    {
        std::cout << backend->dumpUserPort();
        return;
    }

    const std::string& subcmd = args[1];

    if (subcmd == "status")
    {
        std::cout << backend->dumpUserPort();
        return;
    }

    if (subcmd == "device")
    {
        std::cout << backend->dumpUserPort();
        return;
    }

    if (subcmd == "lines")
    {
        std::cout << backend->dumpUserPort();
        return;
    }

    if (subcmd == "rs232")
    {
        // userport rs232
        if (args.size() == 2)
        {
            std::cout << backend->dumpUserPortRS232();
            return;
        }

        const std::string& rs232Cmd = args[2];

        if (rs232Cmd == "help" || rs232Cmd == "?")
        {
            std::cout << help();
            return;
        }

        if (rs232Cmd == "test")
        {
            uint8_t testByte = 0x55;
            RS232Device::Parity parity = RS232Device::Parity::None;

            // Multi byte test
            if (args.size() >= 4 && args[3] == "multi")
            {
                std::cout << backend->selfTestUserPortRS232Multi();
                return;
            }

            if (args.size() >= 4 && args[3] == "formats")
            {
                std::cout << backend->selfTestUserPortRS232Formats();
                return;
            }

            if (args.size() >= 4 && args[3] == "flow")
            {
                std::cout << backend->selfTestUserPortRS232FlowControl();
                return;
            }

            if (args.size() >= 4 && args[3] == "errors")
            {
                std::cout << backend->selfTestUserPortRS232Errors();
                return;
            }

            // Optional test byte.
            if (args.size() >= 4)
            {
                try
                {
                    size_t pos = 0;

                    const unsigned long value =
                        std::stoul(args[3], &pos, 16);

                    if (pos != args[3].size())
                    {
                        std::cout
                            << "Error: invalid hexadecimal byte: "
                            << args[3]
                            << "\n";

                        std::cout
                            << "Example: userport rs232 test 55 even\n";

                        return;
                    }

                    if (value > 0xFF)
                    {
                        std::cout
                            << "Error: RS-232 test byte must be between 00 and FF.\n";

                        return;
                    }

                    testByte = static_cast<uint8_t>(value);
                }
                catch (...)
                {
                    std::cout
                        << "Error: invalid hexadecimal byte: "
                        << args[3]
                        << "\n";

                    std::cout
                        << "Example: userport rs232 test 55 even\n";

                    return;
                }
            }

            // Optional parity mode.
            if (args.size() >= 5)
            {
                const std::string& parityArg = args[4];

                if (parityArg == "none")
                    parity = RS232Device::Parity::None;
                else if (parityArg == "odd")
                    parity = RS232Device::Parity::Odd;
                else if (parityArg == "even")
                    parity = RS232Device::Parity::Even;
                else
                {
                    std::cout << "Error: parity must be none, odd, or even.\n";
                    return;
                }
            }

            if (args.size() > 5)
            {
                std::cout << "Error: too many arguments.\n" << "Usage: userport rs232 test <byte> <parity>\n";
                return;
            }

            std::cout << backend->selfTestUserPortRS232(testByte, parity);
            return;
        }

        std::cout << "Unknown RS-232 subcommand: " << rs232Cmd << "\n";
        std::cout << "Try: userport help\n";
        return;
    }

    std::cout << "Unknown User Port subcommand: " << subcmd << "\n";
    std::cout << "Try: userport help\n";
}
