#ifndef DRIVECHIPS_H_INCLUDED
#define DRIVECHIPS_H_INCLUDED

#include <cstdint>

class DriveMemoryBase {
public:
    virtual ~DriveMemoryBase() = default;
};

class DriveCIABase {
public:
    virtual ~DriveCIABase() = default;
};

class DriveVIABase {
public:
    virtual ~DriveVIABase() = default;

    // ML Monitor
    struct viaRegsView
    {
        const uint8_t& orbIRB;
        const uint8_t& oraIRA;
        const uint8_t& ddrB;
        const uint8_t& ddrA;
        const uint8_t& t1CL;
        const uint8_t& t1CH;
        const uint8_t& t1LL;
        const uint8_t& t1LH;
        const uint8_t& t2CL;
        const uint8_t& t2CH;
        const uint8_t& sr;
        const uint8_t& acr;
        const uint8_t& pcr;
        const uint8_t& ifr;
        const uint8_t& ier;
        const uint8_t& oraNoHS;
    };

    struct MechanicsInfo
    {
        bool valid;          // false = this VIA doesn't have mechanics info
        bool motorOn;
        bool ledOn;
        uint8_t densityCode; // 0–3, if used by this model
    };

    virtual bool checkIRQActive() const = 0;
    virtual viaRegsView getRegsView() const = 0;
    virtual MechanicsInfo getMechanicsInfo() const
    {
        MechanicsInfo m{};
        m.valid = false;
        m.motorOn = false;
        m.ledOn = false;
        m.densityCode = 0;
        return m;
    }
};

#endif // DRIVECHIPS_H_INCLUDED
