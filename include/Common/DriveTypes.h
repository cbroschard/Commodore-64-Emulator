// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef DRIVETYPES_H_INCLUDED
#define DRIVETYPES_H_INCLUDED

// Drive models
enum class DriveModel : uint8_t
{
    None  = 0,
    D1541 = 1,
    D1571 = 2,
    D1581 = 3
};

inline const char* driveModelToString(DriveModel m)
{
    switch (m)
    {
        case DriveModel::None:  return "none";
        case DriveModel::D1541: return "1541";
        case DriveModel::D1571: return "1571";
        case DriveModel::D1581: return "1581";
        default: return "None";
    }
}

static inline bool isValidDriveModelId(uint8_t id)
{
    switch (static_cast<DriveModel>(id))
    {
        case DriveModel::None:
        case DriveModel::D1541:
        case DriveModel::D1571:
        case DriveModel::D1581:
            return true;
    }
    return false;
}

#endif // DRIVETYPES_H_INCLUDED
