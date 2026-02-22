// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef VERSION_H_INCLUDED
#define VERSION_H_INCLUDED

struct VersionInfo
{
    static constexpr const char* NAME = "Commodore 64 Emulator by Christopher Broschard";
    static constexpr const char* VERSION = "v0.7.1-alpha";
    static constexpr const char* BUILD_DATE = __DATE__;
    static constexpr const char* BUILD_TIME = __TIME__;
};

#endif // VERSION_H_INCLUDED
