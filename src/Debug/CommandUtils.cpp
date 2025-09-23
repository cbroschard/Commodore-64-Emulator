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
    if (arg.empty())
    {
        throw std::runtime_error("Invalid address format: empty");
    }

    if (arg[0] == '$')
    {
        return static_cast<uint16_t>(std::stoul(arg.substr(1), nullptr, 16));
    }
    else if (arg.rfind("0x", 0) == 0 || arg.rfind("0X", 0) == 0)
    {
        return static_cast<uint16_t>(std::stoul(arg.substr(2), nullptr, 16));
    }
    else if (arg.find_first_of("ABCDEFabcdef") != std::string::npos)
    {
        return static_cast<uint16_t>(std::stoul(arg, nullptr, 16));
    }
    else
    {
        return static_cast<uint16_t>(std::stoul(arg, nullptr, 10));
    }
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
