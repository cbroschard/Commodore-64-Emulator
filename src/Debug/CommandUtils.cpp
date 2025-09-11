// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
 #include "Debug/CommandUtils.h"

 uint16_t parseAddress(const std::string& arg)
 {
    uint16_t value = 0;

    try {
        if (!arg.empty() && arg[0] == '$') {
            // Hex with $ prefix
            std::stringstream ss;
            ss << std::hex << arg.substr(1);
            ss >> value;
        } else if (arg.rfind("0x", 0) == 0 || arg.rfind("0X", 0) == 0) {
            // Hex with 0x prefix
            std::stringstream ss;
            ss << std::hex << arg.substr(2);
            ss >> value;
        } else {
            // Decimal
            std::stringstream ss(arg);
            ss >> value;
        }
    }
    catch (...) {
        throw std::runtime_error("Invalid address format: " + arg);
    }

    return value;
}

void printPaged(const std::string& text, int pageSize)
{
    std::istringstream iss(text);
    std::string line;
    int lineCount = 0;

    while (std::getline(iss, line))
    {
        std::cout << line << "\n";
        if (++lineCount % pageSize == 0)
        {
            std::cout << "--more-- (q to quit) ";
            char c;
            std::cin.get(c);
            if (c == 'q' || c == 'Q') break;
        }
    }
}
