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

std::string hex2(uint8_t value)
{
    std::ostringstream s;
    s << std::uppercase
      << std::hex
      << std::setw(2)
      << std::setfill('0')
      << static_cast<unsigned>(value);
    return s.str();
}

std::string hex4(uint8_t high, uint8_t low)
{
    std::ostringstream s;
    s << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
        << static_cast<unsigned>((high << 8) | low);
    return s.str();
}

inline std::string trimCopy(std::string s)
{
    auto notSpace = [](int ch){ return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

inline std::string sanitizeAddrToken(std::string s) {
    s = trimCopy(std::move(s));
    s.erase(std::remove(s.begin(), s.end(), '_'), s.end()); // allow underscores
    if (!s.empty() && (s.back()=='h' || s.back()=='H')) s.pop_back(); // allow trailing h
    return s;
}

std::pair<uint16_t,uint16_t> parseRangePair(std::string input)
{
    if (input.empty()) throw std::runtime_error("Invalid range: empty");
    std::string s = trimCopy(std::move(input));

    // Normalize separators
    if (auto p = s.find(".."); p != std::string::npos) s.replace(p, 2, "-");
    if (auto p = s.find(':');  p != std::string::npos) s[p] = '-';

    auto dash = s.find('-');
    if (dash == std::string::npos)
    {
        uint16_t a = parseAddress(sanitizeAddrToken(s));
        return {a, a};
    }

    std::string left  = sanitizeAddrToken(s.substr(0, dash));
    std::string right = sanitizeAddrToken(s.substr(dash + 1));
    if (left.empty() || right.empty()) throw std::runtime_error("Invalid range: missing endpoint");

    uint16_t a = parseAddress(left);
    uint16_t b = parseAddress(right);
    if (a > b) std::swap(a, b);
    return {a, b};
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
