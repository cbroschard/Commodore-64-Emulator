// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef MAIN_H_INCLUDED
#define MAIN_H_INCLUDED

#define SDL_MAIN_HANDLED

#include <boost/program_options.hpp>
#include <boost/version.hpp>
#include <sdl2/sdl.h>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include "Computer.h"
#include "Version.h"

namespace po = boost::program_options;

// Command line options function
po::options_description get_options();

// Config file options function
po::options_description get_config_file_options();

// Parse the configuration file for joystick 1 and 2
JoystickMapping parseJoystickConfig(const std::string& config);

// Helper to parse config items
std::vector<std::string> splitCSV(const std::string& input);

#endif // MAIN_H_INCLUDED
