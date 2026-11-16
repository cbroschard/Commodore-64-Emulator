#ifndef D4040VIA_H
#define D4040VIA_H

#include <cstdint>
#include "Drive/DriveVIA6522.h"
#include "StateReader.h"
#include "StateWriter.h"

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
};

#endif // D4040VIA_H
