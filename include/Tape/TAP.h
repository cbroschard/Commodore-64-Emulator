// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef TAP_H
#define TAP_H

#include "TapeImage.h"

class TAP : public TapeImage
{
    public:
        TAP();
        virtual ~TAP();
        virtual bool loadTape(const std::string& filePath) override;
        virtual void simulateLoading() override;
        bool currentBit() const override;

        void attachLoggingInstance(Logging* logger);

    protected:
        std::vector<uint8_t> tapeData; // vector to store the tape file

    private:

        // Non-owning pointers
        Logging* logger = nullptr;

        #pragma pack(push,1)
        struct tapeHeader
        {
            char fileSignature[12]; // Should equate to either C64-TAPE-RAW or C16-TAPE-RAW depending on version
            uint8_t tapeVersion;
            uint8_t platform;       // 0 = C64, 1 = VIC-20, 2 = C16, Plus/4, 3 = PET, 4 = C5x0, 5 = C6x0, C7x0
            uint8_t videoStandard;  // 0 = PAL, 1 = NTSC, 2 = OLD NTSC, 3 = PALN
            uint8_t reserved;
            uint32_t dataSize;      // File data size without tape header in LOW/HIGH format
        } header;
        #pragma pack(pop)

        // Process pulses
        struct tapePulse
        {
            uint16_t duration;
        };

        std::vector<tapePulse> parsePulses();
        std::vector<tapePulse> pulses;
        size_t pulseIndex;
        uint32_t pulseRemaining;
        bool currentLevel;

        // Loading and validation
        virtual bool loadFile(const std::string& path, std::vector<uint8_t>& buffer) override;
        virtual bool validateHeader() override;
};

#endif // TAP_H
