// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef CAPTUREMAPPER_H
#define CAPTUREMAPPER_H

#include "Cartridge/CartridgeMapper.h"
#include "Cartridge/IHasButton.h"

class CaptureMapper : public CartridgeMapper, public IHasButton
{
    public:
        CaptureMapper();
        virtual ~CaptureMapper();

        // State management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        bool loadIntoMemory(uint8_t bank) override;

        inline uint32_t getButtonCount() const override { return 1; }
        const char* getButtonName(uint32_t buttonIndex) const override;
        void pressButton(uint32_t buttonIndex) override;

        void reset() override;

        bool cpuMemoryHandledByMapper(uint16_t address) const override;
        bool romReadHandledByMapper(uint16_t address) const override;
        bool readDrivesBus(uint16_t address) const override;

    protected:

    private:
        enum class Mode : uint8_t
        {
            Normal,
            Freeze
        };

        Mode mode;

        bool registersEnabled;
        bool romHEnabled;

        bool applyMappingAfterLoad() override;

        void pressFreeze();

        bool setMode(Mode newMode);
};

#endif // CAPTUREMAPPER_H
