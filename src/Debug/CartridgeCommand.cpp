// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Debug/CartridgeCommand.h"
#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"

CartridgeCommand::CartridgeCommand() = default;

CartridgeCommand::~CartridgeCommand() = default;

std::string CartridgeCommand::name() const
{
    return "cart";
}

std::string CartridgeCommand::category() const
{
    return "Cartridge";
}

std::string CartridgeCommand::shortHelp() const
{
    return "cart      - Cartridge commands (info, banks, map, switch, unload)";
}

std::string CartridgeCommand::help() const
{
    return
        "cart - Inspect and control the attached cartridge\n"
        "\n"
        "Usage:\n"
        "    cart <subcommand> [args]\n"
        "\n"
        "Subcommands:\n"
        "    info              Show cartridge type, size, wiring mode, GAME/EXROM lines,\n"
        "                      and current bank.\n"
        "    banks             List available CHIP banks and indicate the active one.\n"
        "    map               Show the current cartridge memory mapping.\n"
        "    switch <bank>     Force switch to the given bank number.\n"
        "    unload            Detach the cartridge, reset cartridge lines, and warm reset.\n"
        "    help              Show this help text.\n"
        "\n"
        "Examples:\n"
        "    cart info         Display cartridge type and wiring mode\n"
        "    cart banks        Show all banks and the current active bank\n"
        "    cart map          Show current cartridge memory map\n"
        "    cart switch 3     Switch to bank 3\n"
        "    cart unload       Remove the cartridge and warm reset\n"
        "\n"
        "Notes:\n"
        "    - Most subcommands require a cartridge to be loaded.\n"
        "    - Bank switching only works for cartridge types that support banking.\n";
}

void CartridgeCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() < 2 || isHelp(args[1]))
    {
        std::cout << help() << std::endl;
        return;
    }

    MLMonitorBackend* backend = mon.mlmonitorbackend();

    if (backend == nullptr)
    {
        std::cout << "Monitor backend is not attached.\n";
        return;
    }

    const std::string& subcmd = args[1];

    Cartridge* cart = backend->getCart();
    const bool cartridgeAttached = backend->getCartridgeAttached();

    if (subcmd == "unload")
    {
        if (!cartridgeAttached || cart == nullptr)
        {
            std::cout << "No cartridge to unload.\n";
            return;
        }

        cart->clearCartridge(cartLocation::LO);
        cart->clearCartridge(cartLocation::HI);

        // Reset PLA lines back to inactive.
        cart->setExROMLine(true);
        cart->setGameLine(true);

        backend->detachCartridge();

        std::cout << "Cartridge unloaded.\n";

        // Run a warm reset so the emulator does not keep executing from
        // memory that was just unmapped.
        backend->warmReset();
        return;
    }

    if (!cartridgeAttached || cart == nullptr)
    {
        std::cout << "Error: No cartridge loaded.\n";
        return;
    }

    if (subcmd == "info")
    {
        std::ostringstream out;

        out << "Cartridge: " << cart->getMapperName()
            << " (" << cart->getCartridgeSize() << " KB)\n";

        out << "Wiring mode: ";

        switch (cart->getWiringMode())
        {
            case Cartridge::WiringMode::CART_8K:
                out << "8K\n";
                break;

            case Cartridge::WiringMode::CART_16K:
                out << "16K\n";
                break;

            case Cartridge::WiringMode::CART_ULTIMAX:
                out << "Ultimax\n";
                break;

            default:
                out << "None\n";
                break;
        }

        out << "GAME=" << cart->getGameLine() << "\n";
        out << "EXROM=" << cart->getExROMLine() << "\n";
        out << "Current bank=" << cart->getCurrentBank() << "\n";

        std::cout << out.str();
        return;
    }

    if (subcmd == "banks")
    {
        const auto& sections = cart->getChipSections();

        if (sections.empty())
        {
            std::cout << "No banks available.\n";
            return;
        }

        std::ostringstream out;

        out << "Banks found: " << cart->getNumberOfBanks() << "\n";

        for (const auto& section : sections)
        {
            out << "  Bank " << std::dec << static_cast<int>(section.bankNumber)
                << " | LoadAddr=$"
                << std::uppercase << std::hex
                << std::setw(4) << std::setfill('0')
                << static_cast<int>(section.loadAddress)
                << std::dec << std::setfill(' ')
                << " | Size=" << (section.data.size() / 1024) << " KB";

            if (section.bankNumber == cart->getCurrentBank())
                out << "  <-- Active";

            out << "\n";
        }

        std::cout << out.str();
        return;
    }

    if (subcmd == "map")
    {
        std::ostringstream out;

        out << "Cartridge memory map:\n";

        switch (cart->getWiringMode())
        {
            case Cartridge::WiringMode::CART_8K:
                out << "  $8000-$9FFF -> Cartridge LO";
                out << " (bank " << cart->getCurrentBank() << ")\n";
                out << "  $A000-$BFFF -> RAM/BASIC depending on PLA state\n";
                out << "  $E000-$FFFF -> KERNAL/ROM/RAM depending on PLA state\n";
                break;

            case Cartridge::WiringMode::CART_16K:
                out << "  $8000-$9FFF -> Cartridge LO";
                out << " (bank " << cart->getCurrentBank() << ")\n";
                out << "  $A000-$BFFF -> Cartridge HI";
                out << " (bank " << cart->getCurrentBank() << ")\n";
                out << "  $E000-$FFFF -> KERNAL/ROM/RAM depending on PLA state\n";
                break;

            case Cartridge::WiringMode::CART_ULTIMAX:
                out << "  $8000-$9FFF -> Cartridge LO";
                out << " (Ultimax bank " << cart->getCurrentBank() << ")\n";
                out << "  $A000-$BFFF -> Open/RAM depending on your PLA implementation\n";
                out << "  $E000-$FFFF -> Cartridge HI";
                out << " (Ultimax bank " << cart->getCurrentBank() << ")\n";
                break;

            default:
                out << "  No cartridge mapping active.\n";
                break;
        }

        std::cout << out.str();
        return;
    }

    if (subcmd == "switch")
    {
        if (args.size() < 3)
        {
            std::cout << "Usage: cart switch <bank>\n";
            return;
        }

        try
        {
            const int bank = std::stoi(args[2]);

            if (bank < 0 || bank > 255)
            {
                std::cout << "Invalid bank: " << bank << ".\n";
                return;
            }

            if (cart->setCurrentBank(static_cast<uint8_t>(bank)))
            {
                std::cout << "Switched to bank " << bank << ".\n";
            }
            else
            {
                std::cout << "Invalid bank: " << bank << ".\n";
            }
        }
        catch (const std::exception&)
        {
            std::cout << "Error: invalid bank number '" << args[2] << "'.\n";
        }

        return;
    }

    std::cout << "Unknown cartridge subcommand: " << subcmd << "\n";
    std::cout << "Try: cart help\n";
}
