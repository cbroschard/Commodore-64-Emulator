#ifndef IECDRIVEDIAGNOSTICS_H_INCLUDED
#define IECDRIVEDIAGNOSTICS_H_INCLUDED

#include "IEC/IECSnapshot.h"

class IECDriveDiagnostics
{
public:
    virtual ~IECDriveDiagnostics() = default;

    virtual IECDriveSnapshot snapshotIEC() const = 0;
};

#endif // IECDRIVEDIAGNOSTICS_H_INCLUDED
