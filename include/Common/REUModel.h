// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef REUMODEL_H_INCLUDED
#define REUMODEL_H_INCLUDED

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

enum class REUModel : uint8_t
{
    None = 0,

    Commodore1700,   // 128 KB
    Commodore1764,   // 256 KB
    Commodore1750,   // 512 KB

    Custom1M,
    Custom2M,
    Custom4M,
    Custom8M,
    Custom16M
};

enum class REUSize : uint8_t
{
    None = 0,
    KB128,
    KB256,
    KB512,
    MB1,
    MB2,
    MB4,
    MB8,
    MB16
};

inline REUSize sizeForREUModel(REUModel model)
{
    switch (model)
    {
        case REUModel::Commodore1700: return REUSize::KB128;
        case REUModel::Commodore1764: return REUSize::KB256;
        case REUModel::Commodore1750: return REUSize::KB512;

        case REUModel::Custom1M:      return REUSize::MB1;
        case REUModel::Custom2M:      return REUSize::MB2;
        case REUModel::Custom4M:      return REUSize::MB4;
        case REUModel::Custom8M:      return REUSize::MB8;
        case REUModel::Custom16M:     return REUSize::MB16;

        case REUModel::None:
        default:
            return REUSize::None;
    }
}

inline std::size_t bytesForREUSize(REUSize size)
{
    switch (size)
    {
        case REUSize::KB128: return 128u * 1024u;
        case REUSize::KB256: return 256u * 1024u;
        case REUSize::KB512: return 512u * 1024u;

        case REUSize::MB1:   return 1u  * 1024u * 1024u;
        case REUSize::MB2:   return 2u  * 1024u * 1024u;
        case REUSize::MB4:   return 4u  * 1024u * 1024u;
        case REUSize::MB8:   return 8u  * 1024u * 1024u;
        case REUSize::MB16:  return 16u * 1024u * 1024u;

        case REUSize::None:
        default:
            return 0;
    }
}

inline std::size_t bytesForREUModel(REUModel model)
{
    return bytesForREUSize(sizeForREUModel(model));
}

inline std::string_view displayNameForREUSize(REUSize size)
{
    switch (size)
    {
        case REUSize::KB128: return "128 KB";
        case REUSize::KB256: return "256 KB";
        case REUSize::KB512: return "512 KB";

        case REUSize::MB1:   return "1 MB";
        case REUSize::MB2:   return "2 MB";
        case REUSize::MB4:   return "4 MB";
        case REUSize::MB8:   return "8 MB";
        case REUSize::MB16:  return "16 MB";

        case REUSize::None:
        default:
            return "0 KB";
    }
}

inline std::string_view displaySizeForREUModel(REUModel model)
{
    return displayNameForREUSize(sizeForREUModel(model));
}

inline std::string_view displayNameForREUModel(REUModel model)
{
    switch (model)
    {
        case REUModel::Commodore1700: return "Commodore 1700 REU";
        case REUModel::Commodore1764: return "Commodore 1764 REU";
        case REUModel::Commodore1750: return "Commodore 1750 REU";

        case REUModel::Custom1M:      return "Custom REU 1 MB";
        case REUModel::Custom2M:      return "Custom REU 2 MB";
        case REUModel::Custom4M:      return "Custom REU 4 MB";
        case REUModel::Custom8M:      return "Custom REU 8 MB";
        case REUModel::Custom16M:     return "Custom REU 16 MB";

        case REUModel::None:
        default:
            return "None";
    }
}

#endif // REUMODEL_H_INCLUDED
