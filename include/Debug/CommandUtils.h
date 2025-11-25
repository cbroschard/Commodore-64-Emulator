// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef COMMANDUTILS_H_INCLUDED
#define COMMANDUTILS_H_INCLUDED

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <string>
#include <sstream>
#include <stdexcept>
#include <cstdint>

// Accept address as hex or decimal
uint16_t parseAddress(const std::string& arg);

// Parse a range of memory addresses
std::pair<uint16_t,uint16_t> parseRangePair(std::string input);

// Helpers
std::string hex2(uint8_t value);
std::string hex4(uint8_t high, uint8_t low);
std::string trimCopy(std::string s);
std::string sanitizeAddrToken(std::string s);
void printPaged(const std::string& text, int pageSize = 20);

#endif // COMMANDUTILS_H_INCLUDED
