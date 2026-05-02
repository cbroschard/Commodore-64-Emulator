// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef SIDMODEL_H_INCLUDED
#define SIDMODEL_H_INCLUDED

#include <string>

enum class SIDModel
{
    MOS6581,
    MOS8580
};

inline const char* sidModelToString(SIDModel model)
{
    switch(model)
    {
        case SIDModel::MOS6581:
            return "6581";
        case SIDModel::MOS8580:
            return "8580";
    }

    return "6581";
}

inline SIDModel sidModelFromString(const std::string& model)
{
    if (model == "8580" ||
        model == "MOS8580" ||
        model == "mos8580" ||
        model == "MOS_8580")
    {
        return SIDModel::MOS8580;
    }

    return SIDModel::MOS6581;
}

#endif // SIDMODEL_H_INCLUDED
