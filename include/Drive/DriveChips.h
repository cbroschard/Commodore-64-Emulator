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

        // ML Monitor
        struct ciaRegsView
        {
            const uint8_t& portA;
            const uint8_t& portB;
            const uint8_t& ddrA;
            const uint8_t& ddrB;
            const uint8_t& tAL;
            const uint8_t& tAH;
            const uint8_t& tBL;
            const uint8_t& tBH;
            const uint8_t& tod10;
            const uint8_t& todSec;
            const uint8_t& todMin;
            const uint8_t& todHour;
            const uint8_t& sd;
            const uint8_t& ier;
            const uint8_t& cra;
            const uint8_t& crb;
            const uint16_t& tA;
            const uint16_t& taLAT;
            const uint16_t& tB;
            const uint16_t& tbLAT;
        };

        virtual ciaRegsView getRegsView() const = 0;
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
