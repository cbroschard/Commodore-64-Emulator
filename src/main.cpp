// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "main.h"

po::options_description get_options()
{
    po::options_description desc("Command line options");
    desc.add_options()
        ("help", "Produce the help message")
        ("cartridge", po::value<std::string>(), "Path and filename for cartridge to load on boot")
        ("tape", po::value<std::string>(), "Path and filename for TAP or T64 tape image to load")
        ("program", po::value<std::string>(), "Path and filename for PRG or P00 image to load")
        ("disk", po::value<std::string>(), "Path and filename for Disk image file to load (D64, D81, etc.")
        ("version", "Print version and exit.");
    return desc;
}

po::options_description get_config_file_options()
{
    po::options_description desc("Configuration File Options");
    desc.add_options()
        ("c64.Video.MODE", po::value<std::string>()->required(), "Video Mode NTSC or PAL")
        ("c64.Kernal.ROM", po::value<std::string>()->required(), "Full path and filename of the C64 Kernal to load")
        ("c64.BASIC.ROM", po::value<std::string>()->required(), "Full path and filename of the C64 BASIC ROM to load")
        ("c64.CHAR.ROM", po::value<std::string>()->required(), "Full path and filename of the C64 BASIC ROM to load")
        ("1541.LO.ROM", po::value<std::string>(), "Full path and filename of the 1541 C000 ROM to load")
        ("1541.HI.ROM", po::value<std::string>(), "Full path and filename of the 1541 E000 ROM to load")
        ("1571.ROM", po::value<std::string>(), "Full path and filename of the 1571 ROM to load")
        ("c64.Joy1", po::value<std::string>(), "Joystick 1 key bindings: Up,Down,Left,Right,Fire")
        ("c64.Joy2", po::value<std::string>(), "Joystick 2 key bindings: Up,Down,Left,Right,Fire");
    return desc;
}

int main(int argc, char *argv[])
{
    // Make our c64
    Computer c64;

    // Process configuration file, exit if there are any errors as we won't know how to boot the system
    std::ifstream configFile("commodore.cfg");
    if (!configFile)
    {
        std::cerr << "Error: Unable to open configuration file commodore.cfg exiting!" << std::endl;
        return 1;

    }

    po::variables_map vmConfig;
    po::options_description configFileOptions = get_config_file_options();

    // Validate required options before moving on
    try
    {
        po::store(po::parse_config_file(configFile, configFileOptions), vmConfig);
        po::notify(vmConfig);
    }
    catch (const po::unknown_option& e)
    {
        std::cerr << "Unknown option encountered: " << e.what() << std::endl;
        return 1;
    }
    catch (const po::required_option& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    // Update the video mode
    c64.setVideoMode(vmConfig["c64.Video.MODE"].as<std::string>());

    // Update the C64 object with the locations for memory initialization
    c64.setBASIC_ROM(vmConfig["c64.BASIC.ROM"].as<std::string>());
    c64.setKernalROM(vmConfig["c64.Kernal.ROM"].as<std::string>());
    c64.setCHAR_ROM(vmConfig["c64.CHAR.ROM"].as<std::string>());

    // Update the C64 object with locations for 1541 ROMs if present
    if (vmConfig.count("1541.LO.ROM") && vmConfig.count("1541.HI.ROM"))
    {
        c64.set1541LoROM(vmConfig["1541.LO.ROM"].as<std::string>());
        c64.set1541HiROM(vmConfig["1541.HI.ROM"].as<std::string>());
    }

    // Update the C64 object with lcoations for 1571 ROM if present
    if (vmConfig.count("1571.ROM"))
    {
        c64.set1571ROM(vmConfig["1571.ROM"].as<std::string>());
    }

    if (vmConfig.count("c64.Joy1"))
    {
        auto cfg = parseJoystickConfig(vmConfig["c64.Joy1"].as<std::string>());
        c64.setJoystickConfig(1, cfg);
    }
    else
    {
        // apply defaults if not in config
        JoystickMapping defaults1
        {
            SDL_SCANCODE_W, SDL_SCANCODE_S,
            SDL_SCANCODE_A, SDL_SCANCODE_D,
            SDL_SCANCODE_SPACE
        };
        c64.setJoystickConfig(1, defaults1);
    }

    if (vmConfig.count("c64.Joy2"))
    {
        auto cfg = parseJoystickConfig(vmConfig["c64.Joy2"].as<std::string>());
        c64.setJoystickConfig(2, cfg);
    }
    else
    {
        JoystickMapping defaults2
        {
            SDL_SCANCODE_UP, SDL_SCANCODE_DOWN,
            SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
            SDL_SCANCODE_RETURN
        };
        c64.setJoystickConfig(2, defaults2);
    }

    // Setup command line options
    po::options_description cmdLineOptions = get_options();
    po::variables_map vmCmdLine;
    po::store(po::parse_command_line(argc, argv, cmdLineOptions), vmCmdLine);
    po::notify(vmCmdLine);

    // Parse cmd line options
    if (vmCmdLine.count("help"))
    {
        std::cout << cmdLineOptions << std::endl;
        return 1;
    }
    if (vmCmdLine.count("cartridge"))
    {
        c64.setCartridgeAttached(true);
        c64.setCartridgePath(vmCmdLine["cartridge"].as<std::string>());
    }
    if (vmCmdLine.count("tape"))
    {
        c64.setTapeAttached(true);
        c64.setTapePath(vmCmdLine["tape"].as<std::string>());
    }
    if (vmCmdLine.count("program"))
    {
        c64.setPrgAttached(true);
        c64.setPrgPath(vmCmdLine["program"].as<std::string>());
    }
    if (vmCmdLine.count("disk"))
    {
        c64.setDiskAttached(true);
        c64.setDiskPath(vmCmdLine["disk"].as<std::string>());
    }
    if (vmCmdLine.count("version"))
    {
        std::cout << VersionInfo::NAME
          << " v" << VersionInfo::VERSION
          << " built " << VersionInfo::BUILD_DATE
          << " " << VersionInfo::BUILD_TIME << "\n";
        SDL_version compiled;
        SDL_VERSION(&compiled);
        std::cout << "SDL "
                  << (int)compiled.major << "."
                  << (int)compiled.minor << "."
                  << (int)compiled.patch << "\n";
        std::cout << "Boost "
          << BOOST_VERSION / 100000     << "."
          << BOOST_VERSION / 100 % 1000 << "."
          << BOOST_VERSION % 100        << "\n";
        return 0;
    }

    try
    {
        // Startup the system
        bool boot = c64.boot();
        if (!boot)
        {
            std::cout << "Problem booting" << std::endl;
            return 1;
        }
        else
        {
            return 0;
        }
    }
    catch (const std::runtime_error& error)
    {
        std::cout << "Error: Runtime error exception: " << error.what() << std::endl;
    }
    catch (const std::exception& error)
    {
        std::cout << "Error: General exception: " << error.what() << std::endl;
    }
    catch (...)
    {
        std::cout << "Error: Caught an unknown exception!" << std::endl;
    }
    return 0;
}

JoystickMapping parseJoystickConfig(const std::string& config)
{
    JoystickMapping jm{};
    auto tokens = splitCSV(config);
    if (tokens.size() != 5) {
        throw std::runtime_error("Joystick config must have 5 keys: Up,Down,Left,Right,Fire");
    }

    jm.up    = SDL_GetScancodeFromName(tokens[0].c_str());
    jm.down  = SDL_GetScancodeFromName(tokens[1].c_str());
    jm.left  = SDL_GetScancodeFromName(tokens[2].c_str());
    jm.right = SDL_GetScancodeFromName(tokens[3].c_str());
    jm.fire  = SDL_GetScancodeFromName(tokens[4].c_str());

    if (jm.up == SDL_SCANCODE_UNKNOWN || jm.down == SDL_SCANCODE_UNKNOWN ||
        jm.left == SDL_SCANCODE_UNKNOWN || jm.right == SDL_SCANCODE_UNKNOWN ||
        jm.fire == SDL_SCANCODE_UNKNOWN) {
        throw std::runtime_error("Invalid key name in joystick config: " + config);
    }

    return jm;

}

std::vector<std::string> splitCSV(const std::string& input)
{
    std::vector<std::string> tokens;
    std::istringstream ss(input);
    std::string item;
    while (std::getline(ss, item, ',')) {
        // trim whitespace
        size_t start = item.find_first_not_of(" \t");
        size_t end   = item.find_last_not_of(" \t");
        if (start != std::string::npos)
            tokens.push_back(item.substr(start, end - start + 1));
    }
    return tokens;
}
