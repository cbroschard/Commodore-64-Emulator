#ifndef IECDRIVESYNC_H_INCLUDED
#define IECDRIVESYNC_H_INCLUDED

class IECDriveSync
{
public:
    virtual ~IECDriveSync() = default;

    virtual void forceSyncIEC() = 0;
};

#endif // IECDRIVESYNC_H_INCLUDED
