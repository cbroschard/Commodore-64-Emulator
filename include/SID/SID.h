// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef SID_H
#define SID_H

// Forward declarations
class CPU;
class Vic;

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <queue>
#include <vector>
#include "common.h"
#include "Logging.h"
#include "Debug/TraceManager.h"
#include "SID/Filter.h"
#include "SID/Mixer.h"
#include "SID/RingBuffer.h"
#include "SID/Voice.h"
#include "StateReader.h"
#include "StateWriter.h"

class SID
{
    public:
        SID(double sampleRate);
        virtual ~SID();

        inline void attachCPUInstance(CPU* processor) { this->processor = processor; }
        inline void attachLogInstance(Logging* logger) { this->logger = logger; }
        inline void attachTraceManagerInstance(TraceManager* traceMgr) { this->traceMgr = traceMgr; }
        inline void attachVicInstance(Vic* vicII) { this->vicII = vicII; }

        // State management
        void saveState(StateWriter& wrtr) const;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr);

        // Getter for main emulation loop processing
        double getSidCyclesPerAudioSample() const;

        // Setter for mode (NTSC or PAL)
        void setMode(VideoMode mode);

        // Setter for sample rate pulled in from SDL
        void setSampleRate(double sample);

        // Register read/write
        uint8_t readRegister(uint16_t address);
        void writeRegister(uint16_t address, uint8_t value);

        double generateAudioSample();

        void tick(uint32_t cycles);
        double popSample();

        // Full reset to default power on state
        void reset();

        // ML Monitor access
        std::string dumpRegisters(const std::string& group);
        inline void setLog(bool enable) { setLogging = enable; }

    protected:

    private:

        // Non owning pointers
        CPU* processor;
        Logging* logger;
        TraceManager* traceMgr;
        Vic* vicII;

        // Monitor logging
        bool setLogging;

        // buffer
        RingBuffer<8192> audioBuf;

        VideoMode mode_;

        double hpPrevIn;
        double hpPrevOut;
        static constexpr double HP_ALPHA = 0.9997;

        // Frequency and sample rate
        double sidClockFrequency;
        double sidCyclesPerAudioSample;
        double sampleRate;

        double sidCycleCounter;

        // Synthesis components:
        Voice voice1;
        Voice voice2;
        Voice voice3;
        Filter filterobj;
        Mixer mixerobj;

        static constexpr double SID_ATTACK_S[16] = {
         0.002, 0.008, 0.016, 0.024,
         0.038, 0.056, 0.068, 0.080,
         0.100, 0.250, 0.500, 0.800,
         1.000, 3.000, 5.000, 8.000
        };

        static constexpr double SID_DECAY_RELEASE_S[16] = {
         0.006, 0.024, 0.048, 0.072,
         0.114, 0.168, 0.204, 0.240,
         0.300, 0.750, 1.500, 2.400,
         3.000, 9.000, 15.000, 24.000
        };

        // Voice structure
        struct voiceRegisters
        {
            uint8_t frequencyLow;
            uint8_t frequencyHigh;
            uint8_t pulseWidthLow;
            uint8_t pulseWidthHigh;
            uint8_t control;
            uint8_t attackDecay;
            uint8_t sustainRelease;
        };

        // Filter structure
        struct filterRegister
        {
            uint8_t cutoffLow;
            uint8_t cutoffHigh;
            uint8_t resonanceControl;
            uint8_t volume;
        };

        // Registers structure
        struct SIDRegisters
        {
            voiceRegisters voice1;
            voiceRegisters voice2;
            voiceRegisters voice3;
            filterRegister filter;
        } sidRegisters;

        // Helpers
        uint16_t combineBytes(uint8_t high, uint8_t low);
        void updateEnvelopeParameters(Voice &voice, voiceRegisters &regs);
        void updateCutoffFromRegisters();

        // Monitor helpers
        std::string decodeControlRegister(uint8_t control) const;
        std::string decodeADSR(const voiceRegisters& regs, const Voice& voice, int index) const;
        std::string dumpVoice(const voiceRegisters& regs, const Voice& voice, int index) const;
};
#endif // SID_H
