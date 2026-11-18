#ifndef D4040VIA_H
#define D4040VIA_H

#include <cstdint>
#include "Drive/DriveVIA6522.h"
#include "IEEE488/IEEE488Bus.h"
#include "StateReader.h"
#include "StateWriter.h"

struct D4040MotorSignals
{
    bool motor0On = false;
    bool motor1On = false;

    uint8_t stepper0 = 0;
    uint8_t stepper1 = 0;
};

class D4040VIA : public DriveVIA6522
{
    public:
        D4040VIA();
        ~D4040VIA();

        void reset() override;

        void saveState(StateWriter& wrtr) const;
        bool loadState(StateReader& rdr);

        uint8_t readRegister(uint16_t address);
        void writeRegister(uint16_t address, uint8_t value);
        uint8_t peek(uint16_t address) const;

        // Drive mechanics
        inline bool isLedOn() const { return ledOn; }
        inline void setLed(bool on) { ledOn = on; }
        inline bool isSyncDetected() const { return syncDetected; }
        inline bool mechHasBytePending() const { return mechBytePending; }
        inline void setSyncDetected(bool present) { syncDetected = present; }
        inline uint8_t getDiskWriteLatch() const { return registers.oraIRA; }
        void diskByteFromMedia(uint8_t byte, bool inSync);
        D4040MotorSignals getMotorSignals() const;

        // Helpers
        void clearIEEE488TransientState();
        void clearMechBytePending();
        void resetShift();
        void pulseWriteByteReady();

        MechanicsInfo getMechanicsInfo() const override;

    protected:
        // Controller hardware inputs
        uint8_t samplePortAInputs() override;
        uint8_t samplePortBInputs() override;

        // Controller hardware outputs
        void onPortAOutputChanged(uint8_t oldValue, uint8_t newValue) override;
        void onPortBOutputChanged(uint8_t oldValue, uint8_t newValue) override;
        void onDDRAChanged(uint8_t oldDDR, uint8_t newDDR) override;
        void onDDRBChanged(uint8_t oldDDR, uint8_t newDDR) override;
        void onPCRChanged(uint8_t oldValue, uint8_t newValue) override;
        void onAttachedToPeripheral() override;

    private:
        // Drive Mechanics
        bool    ledOn;
        bool    syncDetected;
        uint8_t mechDataLatch;
        bool    mechBytePending;
};

#endif // D4040VIA_H
