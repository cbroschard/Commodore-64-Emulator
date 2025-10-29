// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "CPU.h"
#include "Vic.h"
#include "SID/SID.h"

SID::SID(double sampleRate) :
    processor(nullptr),
    logger(nullptr),
    traceMgr(nullptr),
    vicII(nullptr),
    setLogging(false),
    mode_(VideoMode::NTSC), // default to NTSC
    hpPrevIn(0.0),
    hpPrevOut(0.0),
    sampleRate(sampleRate),
    sidCycleCounter(0.0),
    voice1(sampleRate),
    voice2(sampleRate),
    voice3(sampleRate),
    filterobj(sampleRate)
{
    // Zero initialize all registers in the SID structure.
    std::memset(&sidRegisters, 0, sizeof(sidRegisters));
    sidRegisters.filter.volume = 0x00; // All voices muted volume
}

SID::~SID() = default;

double SID::getSidCyclesPerAudioSample() const
{
    return sidCyclesPerAudioSample;
}

void SID::setMode(VideoMode mode)
{
    mode_ = mode;

    if (mode_ == VideoMode::NTSC)
    {
        sidClockFrequency  = 1022727.0; // NTSC SID clock frequency (Hz)
    }
    else
    {
        sidClockFrequency = 985248.0;  // PAL SID clock frequency (Hz)
    }

    sidCyclesPerAudioSample = sidClockFrequency / sampleRate;

    // Update the voices
    voice1.setSIDClockFrequency(sidClockFrequency);
    voice2.setSIDClockFrequency(sidClockFrequency);
    voice3.setSIDClockFrequency(sidClockFrequency);

    // Update the filter
    filterobj.setSIDClockFrequency(sidClockFrequency);
}

void SID::setSampleRate(double sample)
{
    sampleRate = sample;
    sidCyclesPerAudioSample = sidClockFrequency / sampleRate;

    // Propagate out
    voice1.getOscillator().setSampleRate(sample);
    voice2.getOscillator().setSampleRate(sample);
    voice3.getOscillator().setSampleRate(sample);
    filterobj.setSampleRate(sample);
    voice1.getEnvelope().setSampleRate(sample);
    voice2.getEnvelope().setSampleRate(sample);
    voice3.getEnvelope().setSampleRate(sample);
}

uint8_t SID::readRegister(uint16_t address)
{
    switch(address)
    {
        case 0xD400:
        {
            return sidRegisters.voice1.frequencyLow;
        }
        case 0xD401:
        {
            return sidRegisters.voice1.frequencyHigh;
        }
        case 0xD402:
        {
            return sidRegisters.voice1.pulseWidthLow;
        }
        case 0xD403:
        {
            return sidRegisters.voice1.pulseWidthHigh;
        }
        case 0xD404:
        {
            return sidRegisters.voice1.control;
        }
        case 0xD405:
        {
            return sidRegisters.voice1.attackDecay;
        }
        case 0xD406:
        {
            return sidRegisters.voice1.sustainRelease;
        }
        case 0xD407:
        {
            return sidRegisters.voice2.frequencyLow;
        }
        case 0xD408:
        {
            return sidRegisters.voice2.frequencyHigh;
        }
        case 0xD409:
        {
            return sidRegisters.voice2.pulseWidthLow;
        }
        case 0xD40A:
        {
            return sidRegisters.voice2.pulseWidthHigh;
        }
        case 0xD40B:
        {
            return sidRegisters.voice2.control;
        }
        case 0xD40C:
        {
            return sidRegisters.voice2.attackDecay;
        }
        case 0xD40D:
        {
            return sidRegisters.voice2.sustainRelease;
        }
        case 0xD40E:
        {
            return sidRegisters.voice3.frequencyLow;
        }
        case 0xD40F:
        {
            return sidRegisters.voice3.frequencyHigh;
        }
        case 0xD410:
        {
            return sidRegisters.voice3.pulseWidthLow;
        }
        case 0xD411:
        {
            return sidRegisters.voice3.pulseWidthHigh;
        }
        case 0xD412:
        {
            return sidRegisters.voice3.control;
        }
        case 0xD413:
        {
            return sidRegisters.voice3.attackDecay;
        }
        case 0xD414:
        {
            return sidRegisters.voice3.sustainRelease;
        }
        case 0xD415:
        {
            return sidRegisters.filter.cutoffLow;
        }
        case 0xD416:
        {
            return sidRegisters.filter.cutoffHigh;
        }
        case 0xD417:
        {
            return sidRegisters.filter.resonanceControl;
        }
        case 0xD418:
        {
            return sidRegisters.filter.volume;
        }
        case 0xD419:
        {
            // read only register should return joy1 value; using random instead
            return rand() & 0xFF;
        }
        case 0xD41A:
        {
            // read only register should return joy2 value; using random instead
            return rand() & 0xFF;
        }
        case 0xD41B:
        {
            // read only register returns osc voice 3 value
            return rand() & 0xFF;
        }
        case 0xD41C:
        {
            // read only register returns env voice  3
            return rand() & 0xFF;
        }
    }
    // Default value
    return 0xFF;
}

void SID::writeRegister(uint16_t address, uint8_t value)
{
    if (traceMgr->isEnabled() && traceMgr->catOn(TraceManager::TraceCat::SID))
    {
        TraceManager::Stamp stamp = traceMgr->makeStamp(processor ? processor->getTotalCycles() : 0, vicII ? vicII->getCurrentRaster() : 0,
                vicII ? vicII->getRasterDot() : 0);
        traceMgr->recordSidWrite(address & 0x1F, value, stamp);
    }
    switch(address)
    {
        case 0xD400:
        {
            // Update the register and the voice1 frequency at the same time
            sidRegisters.voice1.frequencyLow = value;
            uint16_t freq = combineBytes(sidRegisters.voice1.frequencyHigh, sidRegisters.voice1.frequencyLow);
            voice1.setFrequency(freq);
            break;
        }
        case 0xD401:
        {
            // Update the register and the voice1 frequency at the same time
            sidRegisters.voice1.frequencyHigh = value;
            uint16_t freq = combineBytes(sidRegisters.voice1.frequencyHigh, sidRegisters.voice1.frequencyLow);
            voice1.setFrequency(freq);
            break;
        }
        case 0xD402:
        {
            // Update the register and the voice1 pulse width at the same time
            sidRegisters.voice1.pulseWidthLow = value;
            uint16_t pulseWidth = combineBytes(sidRegisters.voice1.pulseWidthHigh, sidRegisters.voice1.pulseWidthLow);
            voice1.setPulseWidth(pulseWidth);
            break;
        }
        case 0xD403:
        {
            // Update the register and the voice1 pulse width at the same time
            sidRegisters.voice1.pulseWidthHigh = value;

            // Only bits 0-3 make up pulsewidthHigh
            uint16_t pulseWidth = combineBytes(sidRegisters.voice1.pulseWidthHigh & 0x0F, sidRegisters.voice1.pulseWidthLow);
            voice1.setPulseWidth(pulseWidth);
            break;
        }
        case 0xD404:
        {
            // Update the control value and process envelope updates
            sidRegisters.voice1.control = value;

            // Update the voice control value to update the oscillator
            voice1.setControl(value);

            // Use the gate bit to trigger or release the envelope
            if (value & 0x01)
            {
                voice1.trigger();
            }
            else
            {
                voice1.release();
            }
            break;
        }
        case 0xD405:
        {
            sidRegisters.voice1.attackDecay = value;

            // Process and set the parameters for Attack and Delay
            updateEnvelopeParameters(voice1, sidRegisters.voice1);
            break;
        }
        case 0xD406:
        {
            sidRegisters.voice1.sustainRelease = value;

            // Process and set the parameters for Attack and Delay
            updateEnvelopeParameters(voice1, sidRegisters.voice1);
            break;
        }
        case 0xD407:
        {
            // Update the value and voice2 at the same time
            sidRegisters.voice2.frequencyLow = value;
            uint16_t freq = combineBytes(sidRegisters.voice2.frequencyHigh, sidRegisters.voice2.frequencyLow);
            voice2.setFrequency(freq);
            break;
        }
        case 0xD408:
        {
            // Update the value and voice2 at the same time
            sidRegisters.voice2.frequencyHigh = value;

            uint16_t freq = combineBytes(sidRegisters.voice2.frequencyHigh, sidRegisters.voice2.frequencyLow);
            voice2.setFrequency(freq);
            break;
        }
        case 0xD409:
        {
            // Update the value and voice2 at the same time
            sidRegisters.voice2.pulseWidthLow = value;

            // Only bits 0-3 make up the pulseWidthHigh
            uint16_t pulseWidth = combineBytes(sidRegisters.voice2.pulseWidthHigh & 0x0F, sidRegisters.voice2.pulseWidthLow);
            voice2.setPulseWidth(pulseWidth);
            break;
        }
        case 0xD40A:
        {
            // Update the value and voice2 at the same time
            sidRegisters.voice2.pulseWidthHigh = value;

            // Only bits 0-3 make up the pulseWidthHigh
            uint16_t pulseWidth = combineBytes(sidRegisters.voice2.pulseWidthHigh & 0x0F, sidRegisters.voice2.pulseWidthLow);
            voice2.setPulseWidth(pulseWidth);
            break;
        }
        case 0xD40B:
        {
            // Update the control value and process envelope updates
            sidRegisters.voice2.control = value;

            // Set up SYNC and RING MOD relationships
            voice2.getOscillator().setSyncSource(&voice1.getOscillator());
            voice2.getOscillator().setRingSource(&voice1.getOscillator());

            // Update the control value in voice to update the oscillator
            voice2.setControl(value);

            // Use the gate bit to trigger or release the envelope
            if (value & 0x01)
            {
                voice2.trigger();
            }
            else
            {
                voice2.release();
            }
            break;
        }
        case 0xD40C:
        {
            sidRegisters.voice2.attackDecay = value;

            // Process and set the parameters for Attack and Delay
            updateEnvelopeParameters(voice2, sidRegisters.voice2);
            break;
        }
        case 0xD40D:
        {
            sidRegisters.voice2.sustainRelease = value;

            // Process and set the parameters for Attack and Delay
            updateEnvelopeParameters(voice2, sidRegisters.voice2);
            break;
        }
        case 0xD40E:
        {
            // Update the value and voice3 at the same time
            sidRegisters.voice3.frequencyLow = value;
            uint16_t freq = combineBytes(sidRegisters.voice3.frequencyHigh, sidRegisters.voice3.frequencyLow);
            voice3.setFrequency(freq);
            break;
        }
        case 0xD40F:
        {
            // Update the value and voice3 at the same time
            sidRegisters.voice3.frequencyHigh = value;
            uint16_t freq = combineBytes(sidRegisters.voice3.frequencyHigh, sidRegisters.voice3.frequencyLow);
            voice3.setFrequency(freq);
            break;
        }
        case 0xD410:
        {
            // Update the value and voice3 at the same time
            sidRegisters.voice3.pulseWidthLow = value;

            // Only bits 0-3 make up pulseWidthHigh
            uint16_t pulseWidth = combineBytes(sidRegisters.voice3.pulseWidthHigh & 0x0F, sidRegisters.voice3.pulseWidthLow);
            voice3.setPulseWidth(pulseWidth);
            break;
        }
        case 0xD411:
        {
            // Update the value and voice3 at the same time
            sidRegisters.voice3.pulseWidthHigh = value;

            // Only bits 0-3 make up pulseWidthHigh
            uint16_t pulseWidth = combineBytes(sidRegisters.voice3.pulseWidthHigh & 0x0F, sidRegisters.voice3.pulseWidthLow);
            voice3.setPulseWidth(pulseWidth);
            break;
        }
        case 0xD412:
        {
            // Update the control value and process envelope updates
            sidRegisters.voice3.control = value;

            // Set up SYNC and RING MOD relationships
            voice3.getOscillator().setSyncSource(&voice2.getOscillator());
            voice3.getOscillator().setRingSource(&voice2.getOscillator());

            // Update the voice control value to update the oscillator
            voice3.setControl(value);

            // Use the gate bit to trigger or release the envelope
            if (value & 0x01)
            {
                voice3.trigger();
            }
            else
            {
                voice3.release();
            }
            break;
        }
        case 0xD413:
        {
            sidRegisters.voice3.attackDecay = value;

            // Process and set the parameters for Attack and Delay
            updateEnvelopeParameters(voice3, sidRegisters.voice3);
            break;
        }
        case 0xD414:
        {
            sidRegisters.voice3.sustainRelease = value;

            // Process and set the parameters for Attack and Delay
            updateEnvelopeParameters(voice3, sidRegisters.voice3);
            break;
        }
        case 0xD415:
        {
            // Update the value and the filter at the same time
            sidRegisters.filter.cutoffLow = value;
            updateCutoffFromRegisters();
            break;
        }
        case 0xD416:
        {
            // Update the value and the filter at the same time
            sidRegisters.filter.cutoffHigh = value;
            updateCutoffFromRegisters();
            break;
        }
        case 0xD417:
        {
            // Update the value and the filter at the same time
            sidRegisters.filter.resonanceControl = value;

            // Set filter mode bits (bit 0 = LP, bit 1 = BP, bit 2 = HP)
            filterobj.setMode(value & 0x07);

            filterobj.setResonance(value);
            break;
        }
        case 0xD418:
        {
            sidRegisters.filter.volume = value;
            break;
        }
        case 0xD419:
        {
            // read only register
            break;
        }
        case 0xD41A:
        {
            // read only registers
            break;
        }
        case 0xD41B:
        {
            // read only register
            break;
        }
        case 0xD41C:
        {
            // read only register
            break;
        }
    }
}

double SID::generateAudioSample()
{
    if (voice1.getOscillator().didOverflow() && (sidRegisters.voice2.control & 0x02))
    {
        voice2.getOscillator().resetPhase();
    }
    if (voice2.getOscillator().didOverflow() && (sidRegisters.voice3.control & 0x02))
    {
        voice3.getOscillator().resetPhase();
    }

    std::vector<double> filteredVoices;
    std::vector<double> unfilteredVoices;
    uint8_t routeBits = sidRegisters.filter.resonanceControl;  // bits 4/5/6

    for (int i = 0; i < 3; ++i)
    {
        Voice* v = (i == 0 ? &voice1 : i == 1 ? &voice2 : &voice3);
        static const int routeMask[3] = { 1<<6, 1<<5, 1<<4 };   // v1,v2,v3
        bool route = routeBits & routeMask[i];
        //bool route = routeBits & (1 << (4 + i));
        v->setFilterRouted(route);

        double s = v->generateVoiceSample();
        if (route)
        {
            filteredVoices.push_back(s);
        }
        else
        {
            unfilteredVoices.push_back(s);
        }
    }

    double filteredMix   = mixerobj.mixSamples(filteredVoices);
    double unfilteredMix = mixerobj.mixSamples(unfilteredVoices);

    double filteredOut = 0.0;
    if (routeBits & 0x07)
    {
        filteredOut = filterobj.processSample((filteredMix));
    }

    double mixed = filteredOut + unfilteredMix;
    double dacLevel = ((sidRegisters.filter.volume & 0xF0) >> 4) / 15.0;
    mixed += dacLevel * 0.2;

    double hp = HP_ALPHA * (hpPrevOut + mixed - hpPrevIn);
    hpPrevIn  = mixed;
    hpPrevOut = hp;

    double masterVol = (sidRegisters.filter.volume & 0x0F) / 15.0;
    return hp * masterVol;
}

void SID::tick(uint32_t cycles)
{
    sidCycleCounter += static_cast<double>(cycles);
    size_t samplesToPush = static_cast<size_t>(sidCycleCounter / sidCyclesPerAudioSample);
    sidCycleCounter -= samplesToPush * sidCyclesPerAudioSample;

    for (size_t i = 0; i < samplesToPush; ++i)
    {
        double sample = generateAudioSample();
        audioBuf.push(sample);
    }
}

double SID::popSample()
{
    double s;
    if (audioBuf.pop(s))
    {
        return s;
    }
    return 0.0;
}

void SID::reset()
{
    std::memset(&sidRegisters, 0, sizeof(sidRegisters));
    sidRegisters.filter.volume = 0x00;  // Mute audio until set
    sidCycleCounter = 0.0;

    voice1.reset();
    voice2.reset();
    voice3.reset();
    filterobj.reset();
}

uint16_t SID::combineBytes(uint8_t high, uint8_t low)
{
    return (static_cast<uint16_t>(high) << 8) | low;
}

void SID::updateEnvelopeParameters(Voice &voice, voiceRegisters &regs)
{
    // extract rate nibbles
    uint8_t attackIdx  = (regs.attackDecay >> 4) & 0x0F;
    uint8_t decayIdx   =  regs.attackDecay       & 0x0F;
    uint8_t sustainIdx = (regs.sustainRelease >> 4) & 0x0F;
    uint8_t releaseIdx =  regs.sustainRelease      & 0x0F;

    double sustainLevel = static_cast<double>(sustainIdx) / 15.0;

    // look up correct times
    double attackTime  = SID_ATTACK_S[attackIdx];
    double decayTime   = SID_DECAY_RELEASE_S[decayIdx];
    double releaseTime = SID_DECAY_RELEASE_S[releaseIdx];

    voice.setEnvelopeParameters(attackTime, decayTime, sustainLevel, releaseTime);
}

void SID::updateCutoffFromRegisters()
{
    // Extract the 11-bit cutoff value from D416 (high) and D415 (low)
    uint16_t cutoff11bit = ((sidRegisters.filter.cutoffHigh & 0x07) << 8) | sidRegisters.filter.cutoffLow;

    // Normalize and apply SID-like cutoff mapping curve (approx 30Hz – 12kHz)
    double normalized = static_cast<double>(cutoff11bit) / 2047.0;
    double curve = pow(2.0, normalized * 8.0) - 1.0;
    double cutoffFreq = 30.0 + (curve / 255.0) * (12000.0 - 30.0);

    // Safety clamp
    cutoffFreq = std::clamp(cutoffFreq, 30.0, 12000.0);

    // Apply to filter
    filterobj.setCutoffFreq(cutoffFreq);
}

std::string SID::dumpRegisters(const std::string& group)
{
    std::stringstream out;
    out << std::hex << std::uppercase << std::setfill('0');
    if (group == "voice1" || group == "voices" || group == "all") out << dumpVoice(sidRegisters.voice1, voice1, 1);
    if (group == "voice2" || group == "voices" || group == "all") out << dumpVoice(sidRegisters.voice2, voice2, 2);
    if (group == "voice3" || group == "voices" || group == "all") out << dumpVoice(sidRegisters.voice3, voice3, 3);

    if (group == "filter" || group == "all")
    {
        // Filter
        uint16_t cutoff = ((sidRegisters.filter.cutoffHigh & 0x07) << 8) | sidRegisters.filter.cutoffLow;
        out << "Filter:\n";
        out << "  Cutoff=$" << std::hex << std::setw(4) << std::setfill('0') << cutoff
            << "  Res/Route=$" << std::setw(2) << static_cast<int>(sidRegisters.filter.resonanceControl)
            << "  Volume=$" << std::setw(2) << static_cast<int>(sidRegisters.filter.volume) << "\n";
    }

    return out.str();
}

std::string SID::decodeControlRegister(uint8_t ctrl) const
{
    std::stringstream out;
    out << "  CTRL=$" << std::hex << std::setw(2) << static_cast<int>(ctrl) << " (";
    if (ctrl & 0x80) out << "GATE ";
    if (ctrl & 0x40) out << "SYNC ";
    if (ctrl & 0x20) out << "RING ";
    if (ctrl & 0x10) out << "TEST ";
    if (ctrl & 0x08) out << "TRI ";
    if (ctrl & 0x04) out << "SAW ";
    if (ctrl & 0x02) out << "PULSE ";
    if (ctrl & 0x01) out << "NOISE ";
    out << ")\n";
    return out.str();
}

std::string SID::decodeADSR(const voiceRegisters& regs, const Voice& voice, int index) const
{
    std::stringstream out;

    // ADSR nibbles
    uint8_t A = (regs.attackDecay >> 4) & 0x0F;
    uint8_t D = (regs.attackDecay)      & 0x0F;
    uint8_t S = (regs.sustainRelease >> 4) & 0x0F;
    uint8_t R = (regs.sustainRelease)      & 0x0F;

    out << "  ADSR: A=$" << std::hex << static_cast<int>(A) << " D=$" << static_cast<int>(D)
        << " S=$" << static_cast<int>(S) << " R=$" << static_cast<int>(R) << "\n";

    // Current envelope level
    Envelope::State st = voice.getEnvelope().getState();
    double level = voice.getEnvelope().getLevel();
    out << "  ENV State=" << Envelope::stateToString(st) << " Level=" << std::dec << std::fixed << std::setprecision(3) << level << "\n";

    return out.str();
}

std::string SID::dumpVoice(const voiceRegisters& regs, const Voice& voice, int index) const
{
    std::stringstream out;
    out << "SID Voice " << index << ":\n";

    uint16_t freq = (regs.frequencyHigh << 8) | regs.frequencyLow;
    double freqHz = (freq * sidClockFrequency) / 65536.0;
    out << "  FREQ=$" << std::hex << std::setw(4) << std::setfill('0') << freq
        << " (" << std::dec << std::fixed << std::setprecision(1) << freqHz << " Hz)\n";

    uint16_t pw = ((regs.pulseWidthHigh & 0x0F) << 8) | regs.pulseWidthLow;
    double duty = (pw / 4096.0) * 100.0;
    out << "  PW=$" << std::hex << std::setw(4) << std::setfill('0') << pw
        << " (" << std::dec << std::setprecision(1) << duty << "%)\n";

    out << decodeControlRegister(regs.control);
    out << decodeADSR(regs, voice, index);
    return out.str();
}
