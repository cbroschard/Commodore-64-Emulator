#ifndef DRIVECHIPS_H_INCLUDED
#define DRIVECHIPS_H_INCLUDED

#include <cstdint>
#include "CPUBus.h"

class DriveMemoryBase : public CPUBus
{
    public:
        ~DriveMemoryBase() override = default;

        virtual void tick(uint32_t cycles) { (void)cycles; }
};

class DriveCIABase
{
    public:
        virtual ~DriveCIABase() = default;

            // ML Monitor
            struct ciaRegsView
            {
                const uint8_t& portA;
                const uint8_t& portB;
                const uint8_t& ddrA;
                const uint8_t& ddrB;
                const uint8_t& interruptStatus;
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

            struct ciaIECWriteHistoryEntry
            {
                bool valid = false;
                uint16_t pc = 0xFFFF;
                uint16_t retTarget = 0xFFFF;
                uint16_t address = 0;
                uint8_t reg = 0;
                uint8_t value = 0;
                uint8_t prAfter = 0;
                uint8_t ddrAfter = 0;
            };

            struct ciaIECReadHistoryEntry
            {
                bool valid = false;
                uint16_t pc = 0xFFFF;
                uint16_t retTarget = 0xFFFF;
                uint16_t address = 0;
                uint8_t reg = 0;
                uint8_t value = 0;
            };

            struct ciaIECDecodeView
            {
                bool available = false;
                const char* modelName = "";

                uint8_t pr = 0xFF;
                uint8_t ddr = 0x00;

                uint8_t rawPortAPins = 0xFF;
                uint8_t rawPortBPins = 0xFF;

                bool atnInLow = false;
                bool clkInLow = false;
                bool dataInLow = false;
                bool srqInLow = false;

                bool busDirOutput = false;
                bool atnAckDataLow = false;
                bool datOutAssertLow = false;
                bool clkOutAssertLow = false;

                bool resolvedAtnLow = false;
                bool resolvedClkLow = false;
                bool resolvedDataLow = false;

                bool finalDataLow = false;
                bool finalClkLow = false;

                ciaIECWriteHistoryEntry writeHistory[8]{};
                ciaIECReadHistoryEntry readHistory[8]{};

                uint32_t sameReadCount = 0;
                uint8_t lastReadValue = 0xFF;
            };

            virtual ciaRegsView getRegsView() const = 0;

            virtual ciaIECDecodeView getIECDecodeView() const
            {
                return {};
            }

};

class DriveVIABase
{
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

        struct VIATimerDebugView
        {
            uint16_t timer1Counter = 0;
            uint16_t timer1Latch = 0;
            bool timer1Running = false;
            bool timer1JustLoaded = false;
            bool timer1ReloadPending = false;
            bool timer1InhibitIRQ = false;
            bool timer1PB7Level = true;

            uint16_t timer2Counter = 0;
            uint16_t timer2Latch = 0;
            bool timer2Running = false;
            bool timer2JustLoaded = false;
            bool timer2InhibitIRQ = false;
            uint8_t timer2LowLatchByte = 0;
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
        virtual VIATimerDebugView getTimerDebugView() const { return {}; }
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

class DriveFDCBase
{
    public:
        virtual ~DriveFDCBase() = default;

        // ML Monitor: snapshot-by-reference view (NO virtuals in here)
        struct fdcRegsView
        {
            const uint8_t&  status;
            const uint8_t&  command;
            const uint8_t&  track;
            const uint8_t&  sector;
            const uint8_t&  data;

            const bool&     drq;
            const bool&     intrq;

            const uint16_t& currentSectorSize;
            const uint16_t&  dataIndex;

            const bool&     readSectorInProgress;
            const bool&     writeSectorInProgress;

            const int32_t&  cyclesUntilEvent;
        };

        // Polymorphic queries (your dump uses these too)
        virtual bool     checkIRQActive() const = 0;
        virtual bool     checkDRQActive() const = 0;
        virtual uint16_t getSectorSize()  const = 0;

        virtual fdcRegsView getRegsView() const = 0;
};

#endif // DRIVECHIPS_H_INCLUDED
