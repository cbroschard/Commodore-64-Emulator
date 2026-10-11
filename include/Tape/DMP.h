// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef DMP_H
#define DMP_H

#include <cstddef>
#include <cstdint>
#include <vector>
#include "Tape/TapeImage.h"

class DMP : public TapeImage
{
    public:
        DMP();
        ~DMP();

        // State management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        bool loadTape(const std::string& filePath, VideoMode mode) override;
        void rewind() override;
        void simulateLoading() override;
        bool currentBit() const override;

        uint64_t fastForwardCycles(uint64_t cyclesToSkip) override;

        uint64_t totalCycles() const override;
        uint64_t currentCycles() const override;

        bool atEnd() const override;

        // Monitor helpers
        inline uint8_t debugTapeVersion() const override { return header.tapeVersion; }
        inline size_t debugPulseIndex() const override { return pulseIndex; }
        inline size_t debugPulseCount() const override { return pulses.size(); }
        inline uint32_t debugPulseRemaining() const override { return pulseRemaining; }
        uint32_t debugCurrentPulse() const;
        uint32_t debugNextPulse(size_t lookahead = 1) const override;

    protected:
        std::vector<uint8_t> tapeData; // vector to store the tape file

    private:
        static constexpr double PAL_CLOCK = 985248.0;
        static constexpr double NTSC_CLOCK = 1022727.0;

        #pragma pack(push,1)
        struct tapeHeader
        {
            char fileSignature[12];     // $00: "DC2N-TAP-RAW"
            uint8_t tapeVersion;        // $0C: 0 or 1
            uint8_t platform;           // $0D: Machine ID (v0), machine ID + flags (v1)
            uint8_t videoStandard;      // $0E: 0 = PAL, 1 = NTSC
            uint8_t bitsPerSample;      // $0F: Usually 16
            uint32_t counterFrequency;  // $10: Counter frequency in Hz (typically 2000000)
        } header;
        #pragma pack(pop)

        // Process pulses
        struct tapePulse
        {
            uint32_t duration = 0;  // Duration in emulated CPU cycles
            bool toggleLevel = false; // Toggle READ level after duration
        };

        std::vector<tapePulse> pulses;

        // Playback state
        size_t pulseIndex;
        uint32_t pulseRemaining;
        bool currentLevel;

        // DMP recording mode
        bool individualStates;
        bool recordingMode;

        // Falling-edge pulse generation
        uint8_t blipWidth;
        uint8_t blipCountdown;

        // Tape progress
        uint64_t elapsedCycles;
        uint64_t tapeTotalCycles;

        std::vector<tapePulse> parsePulses(VideoMode mode);

        // Loading and validation
        virtual bool loadFile(const std::string& path, std::vector<uint8_t>& buffer) override;
        virtual bool validateHeader() override;
};

#endif // DMP_H
