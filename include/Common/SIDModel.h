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
    if (model == "8580" || "MOS8580" || "mos8580")
            return SIDModel::MOS8580;

    return SIDModel::MOS6581;
}

#endif // SIDMODEL_H_INCLUDED
