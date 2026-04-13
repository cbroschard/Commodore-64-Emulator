// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#pragma once

#define SDL_MAIN_HANDLED

#include <boost/program_options.hpp>
#include <SDL2/SDL.h>
#include <string>
#include <vector>

#include "Common/JoystickMapping.h"

boost::program_options::options_description get_options();
boost::program_options::options_description get_config_file_options();

JoystickMapping parseJoystickConfig(const std::string& config);
std::vector<std::string> splitCSV(const std::string& input);
