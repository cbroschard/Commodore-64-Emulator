// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/CartridgeCommand.h"
#include "Debug/MLMonitor.h"

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
   return "cart      - Cartridge commands (info, banks, map, switch)";
}

std::string CartridgeCommand::help() const
{
    return
        "cart <subcommand> [args]\n"
        "    Inspect and control the attached cartridge.\n"
        "\n"
        "Subcommands:\n"
        "    info              Show cartridge type, wiring mode, GAME/EXROM lines.\n"
        "    banks             List available banks and indicate the active one.\n"
        "    map               Show current $8000-$BFFF/$E000-$FFFF mapping.\n"
        "    switch <bank>     Force switch to the given bank number.\n"
        "    unload            Detach the cartridge from memory.\n"
        "\n"
        "Examples:\n"
        "    cart info         Display cartridge type and wiring mode.\n"
        "    cart banks        Show all banks and the current active bank.\n"
        "    cart switch 3     Switch to bank 3.\n"
        "    cart unload       Remove the cartridge.\n";
}

void CartridgeCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() < 2 || isHelp(args[1]))
    {
        std::cout << help() << std::endl;
        return;
    }

    // Get the pointer to the cart object first
    Cartridge* cart = mon.computer()->getCart();

    if (!cart || !mon.computer()->getCartridgeAttached())
    {
        std::cout << "Error: No cartridge loaded.\n";
        return;
    }

    const std::string& subcmd = args[1];

    if (subcmd == "info")
    {
        std::ostringstream out;
        out << "Cartridge: " << cart->getMapperName();
        out << " (" << cart->getCartridgeSize() << " KB)\n";
        out << "Wiring mode: ";
        switch (cart->getWiringMode())
        {
            case Cartridge::WiringMode::CART_8K:
                out << "  Cartridge wiring mode: 8K\n";
                break;
            case Cartridge::WiringMode::CART_16K:
                out << "  Cartridge wiring mode: 16K\n";
                break;
            case Cartridge::WiringMode::CART_ULTIMAX:
                out << "  Cartridge wiring mode: Ultimax\n";
                break;
            default:
                out << "  Cartridge wiring mode: None\n";
                break;
        }
        out << "GAME=" << cart->getGameLine();
        out << " EXROM=" << cart->getExROMLine();
        out << " Current bank=" << cart->getCurrentBank() << "\n";
        std::cout << out.str();
    }
    else if (subcmd == "banks")
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
                << " | LoadAddr=$" << std::hex << section.loadAddress
                << " | Size=" << std::dec << (section.data.size() / 1024) << " KB";

            if (section.bankNumber == cart->getCurrentBank())
                out << "  <-- Active";

            out << "\n";
        }

        std::cout << out.str();
    }
    else if (subcmd == "map")
    {
        std::ostringstream out;
        out << "Cartridge memory map:\n";

        switch (cart->getWiringMode())
        {
            case Cartridge::WiringMode::CART_8K:
                out << "  $8000-$9FFF -> Cartridge LO (bank "
                    << cart->getCurrentBank() << ")\n";
                out << "  $A000-$BFFF -> RAM\n";
                break;
            case Cartridge::WiringMode::CART_16K:
                out << "  $8000-$9FFF -> Cartridge LO (bank "
                    << cart->getCurrentBank() << ")\n";
                out << "  $A000-$BFFF -> Cartridge HI (bank "
                    << cart->getCurrentBank() << ")\n";
                break;
            case Cartridge::WiringMode::CART_ULTIMAX:
                out << "  $8000-$9FFF -> Cartridge LO (Ultimax bank "
                    << cart->getCurrentBank() << ")\n";
                out << "  $E000-$FFFF -> Cartridge HI (Ultimax bank "
                    << cart->getCurrentBank() << ")\n";
                out << "  $A000-$BFFF -> RAM\n";
                break;
            default:
                out << "  No cartridge mapping active.\n";
                break;
        }

    std::cout << out.str();
    }
    else if (subcmd == "switch")
    {
        if (args.size() < 3)
        {
            std::cout << "Usage: cart switch <bank>\n";
            return;
        }

        try
        {
            int bank = std::stoi(args[2]);

            if (cart->setCurrentBank(static_cast<uint8_t>(bank)))
            {
                std::cout << "Switched to bank " << bank << ".\n";
            }
            else
            {
                std::cout << "Invalid bank: " << bank << ".\n";
            }
        }
        catch (const std::exception& e)
        {
            std::cout << "Error: invalid bank number '" << args[2] << "'.\n";
        }

    }
    else if (subcmd == "unload")
    {
        if (!mon.computer()->getCartridgeAttached())
        {
            std::cout << "No cartridge to unload.\n";
            return;
        }
        if (cart)
        {
            // Clear cartridge memory regions
            cart->clearCartridge(cartLocation::LO);
            cart->clearCartridge(cartLocation::HI);

            // Reset PLA lines
            cart->setExROMLine(true);
            cart->setGameLine(true);
        }

        // Detach from computer
        mon.computer()->detachCartridge();

        std::cout << "Cartridge unloaded.\n";

        // Run a warm reset so the emu doesn't crash with the rug pulled out from it
        mon.computer()->warmReset();
    }
}
