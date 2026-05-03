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
    sidModel_(SIDModel::MOS6581),
    processor(nullptr),
    logger(nullptr),
    traceMgr(nullptr),
    vicII(nullptr),
    setLogging(false),
    mode_(VideoMode::NTSC), // default to NTSC
    hpPrevIn(0.0),
    hpPrevOut(0.0),
    lastOutputSample(0.0),
    underrunOutputSample(0.0),
    recoveryStartSample(0.0),
    audioWasUnderrunning(false),
    underrunRecoverySamples(0),
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

    setSIDModel(SIDModel::MOS6581);

    configureOscillatorSources();
}

SID::~SID() = default;

void SID::saveState(StateWriter& wrtr) const
{
    // SID0 = "core" and registers
    wrtr.beginChunk("SID0");

    // Dump Registers
    wrtr.writeU8(sidRegisters.voice1.frequencyLow);
    wrtr.writeU8(sidRegisters.voice1.frequencyHigh);
    wrtr.writeU8(sidRegisters.voice1.pulseWidthLow);
    wrtr.writeU8(sidRegisters.voice1.pulseWidthHigh);
    wrtr.writeU8(sidRegisters.voice1.control);
    wrtr.writeU8(sidRegisters.voice1.attackDecay);
    wrtr.writeU8(sidRegisters.voice1.sustainRelease);
    wrtr.writeU8(sidRegisters.voice2.frequencyLow);
    wrtr.writeU8(sidRegisters.voice2.frequencyHigh);
    wrtr.writeU8(sidRegisters.voice2.pulseWidthLow);
    wrtr.writeU8(sidRegisters.voice2.pulseWidthHigh);
    wrtr.writeU8(sidRegisters.voice2.control);
    wrtr.writeU8(sidRegisters.voice2.attackDecay);
    wrtr.writeU8(sidRegisters.voice2.sustainRelease);
    wrtr.writeU8(sidRegisters.voice3.frequencyLow);
    wrtr.writeU8(sidRegisters.voice3.frequencyHigh);
    wrtr.writeU8(sidRegisters.voice3.pulseWidthLow);
    wrtr.writeU8(sidRegisters.voice3.pulseWidthHigh);
    wrtr.writeU8(sidRegisters.voice3.control);
    wrtr.writeU8(sidRegisters.voice3.attackDecay);
    wrtr.writeU8(sidRegisters.voice3.sustainRelease);
    wrtr.writeU8(sidRegisters.filter.cutoffLow);
    wrtr.writeU8(sidRegisters.filter.cutoffHigh);
    wrtr.writeU8(sidRegisters.filter.resonanceControl);
    wrtr.writeU8(sidRegisters.filter.volume);

    // End the chunk
    wrtr.endChunk();

    // SIDX = Runtime state
    wrtr.beginChunk("SIDX");

    // Dump current video mode
    wrtr.writeU8(static_cast<uint8_t>(mode_));

    // Dump SID Model
    wrtr.writeU8(static_cast<uint8_t>(sidModel_));

    // Dump fractional accumulator
    wrtr.writeF64(sidCycleCounter);

    // Dump High pass filter history
    wrtr.writeF64(hpPrevIn);
    wrtr.writeF64(hpPrevOut);

    // Dump Voice1 runtime state
    wrtr.writeF64(voice1.getOscillator().getPhase());
    wrtr.writeBool(voice1.getOscillator().getPhaseOverflow());
    wrtr.writeU32(voice1.getOscillator().getNoiseLFSR());
    wrtr.writeU8(static_cast<uint8_t>(voice1.getEnvelope().getState()));
    wrtr.writeF64(voice1.getEnvelope().getLevel());

    // Dump Voice2 runtime status
    wrtr.writeF64(voice2.getOscillator().getPhase());
    wrtr.writeBool(voice2.getOscillator().getPhaseOverflow());
    wrtr.writeU32(voice2.getOscillator().getNoiseLFSR());
    wrtr.writeU8(static_cast<uint8_t>(voice2.getEnvelope().getState()));
    wrtr.writeF64(voice2.getEnvelope().getLevel());

    // Dump Voice3 runtime state
    wrtr.writeF64(voice3.getOscillator().getPhase());
    wrtr.writeBool(voice3.getOscillator().getPhaseOverflow());
    wrtr.writeU32(voice3.getOscillator().getNoiseLFSR());
    wrtr.writeU8(static_cast<uint8_t>(voice3.getEnvelope().getState()));
    wrtr.writeF64(voice3.getEnvelope().getLevel());

    // Dump Filter runtime state
    wrtr.writeF64(filterobj.getLowPassOut());
    wrtr.writeF64(filterobj.getBandPassOut());
    wrtr.writeF64(filterobj.getHighPassOut());
    wrtr.writeF64(filterobj.getDcBlock());

    // End the chunk
    wrtr.endChunk();
}

bool SID::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "SID0", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        // Read registers in the exact order we wrote them.
        if (!rdr.readU8(sidRegisters.voice1.frequencyLow)) return false;
        if (!rdr.readU8(sidRegisters.voice1.frequencyHigh)) return false;
        if (!rdr.readU8(sidRegisters.voice1.pulseWidthLow)) return false;
        if (!rdr.readU8(sidRegisters.voice1.pulseWidthHigh)) return false;
        if (!rdr.readU8(sidRegisters.voice1.control)) return false;
        if (!rdr.readU8(sidRegisters.voice1.attackDecay)) return false;
        if (!rdr.readU8(sidRegisters.voice1.sustainRelease)) return false;

        if (!rdr.readU8(sidRegisters.voice2.frequencyLow)) return false;
        if (!rdr.readU8(sidRegisters.voice2.frequencyHigh)) return false;
        if (!rdr.readU8(sidRegisters.voice2.pulseWidthLow)) return false;
        if (!rdr.readU8(sidRegisters.voice2.pulseWidthHigh)) return false;
        if (!rdr.readU8(sidRegisters.voice2.control)) return false;
        if (!rdr.readU8(sidRegisters.voice2.attackDecay)) return false;
        if (!rdr.readU8(sidRegisters.voice2.sustainRelease)) return false;

        if (!rdr.readU8(sidRegisters.voice3.frequencyLow)) return false;
        if (!rdr.readU8(sidRegisters.voice3.frequencyHigh)) return false;
        if (!rdr.readU8(sidRegisters.voice3.pulseWidthLow)) return false;
        if (!rdr.readU8(sidRegisters.voice3.pulseWidthHigh)) return false;
        if (!rdr.readU8(sidRegisters.voice3.control)) return false;
        if (!rdr.readU8(sidRegisters.voice3.attackDecay)) return false;
        if (!rdr.readU8(sidRegisters.voice3.sustainRelease)) return false;

        if (!rdr.readU8(sidRegisters.filter.cutoffLow)) return false;
        if (!rdr.readU8(sidRegisters.filter.cutoffHigh)) return false;
        if (!rdr.readU8(sidRegisters.filter.resonanceControl)) return false;
        if (!rdr.readU8(sidRegisters.filter.volume)) return false;

        // Re-apply derived state WITHOUT triggering envelopes like writeRegister() would.
        {
            // Frequencies
            uint16_t f1 = combineBytes(sidRegisters.voice1.frequencyHigh, sidRegisters.voice1.frequencyLow);
            uint16_t f2 = combineBytes(sidRegisters.voice2.frequencyHigh, sidRegisters.voice2.frequencyLow);
            uint16_t f3 = combineBytes(sidRegisters.voice3.frequencyHigh, sidRegisters.voice3.frequencyLow);
            voice1.setFrequency(f1);
            voice2.setFrequency(f2);
            voice3.setFrequency(f3);

            // Pulse width (only high nibble used)
            uint16_t pw1 = combineBytes(sidRegisters.voice1.pulseWidthHigh & 0x0F, sidRegisters.voice1.pulseWidthLow);
            uint16_t pw2 = combineBytes(sidRegisters.voice2.pulseWidthHigh & 0x0F, sidRegisters.voice2.pulseWidthLow);
            uint16_t pw3 = combineBytes(sidRegisters.voice3.pulseWidthHigh & 0x0F, sidRegisters.voice3.pulseWidthLow);
            voice1.setPulseWidth(pw1);
            voice2.setPulseWidth(pw2);
            voice3.setPulseWidth(pw3);

            voice1.setControl(sidRegisters.voice1.control);
            voice2.setControl(sidRegisters.voice2.control);
            voice3.setControl(sidRegisters.voice3.control);

            configureOscillatorSources();

            // Envelope parameters derived from ADSR regs
            updateEnvelopeParameters(voice1, sidRegisters.voice1);
            updateEnvelopeParameters(voice2, sidRegisters.voice2);
            updateEnvelopeParameters(voice3, sidRegisters.voice3);

            // Filter derived from regs
            updateCutoffFromRegisters();
            filterobj.setMode((sidRegisters.filter.volume >> 4) & 0x07);
            filterobj.setResonance(sidRegisters.filter.resonanceControl);
        }
        rdr.exitChunkPayload(chunk);
        return true;
    }

    if (std::memcmp(chunk.tag, "SIDX", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint8_t modeU8 = 0;
        if (!rdr.readU8(modeU8)) return false;
        mode_ = static_cast<VideoMode>(modeU8);
        setMode(mode_); // restores sidClockFrequency & sidCyclesPerAudioSample coherently

        uint8_t sidModeU8 = 0;
        if (!rdr.readU8(sidModeU8)) return false;
        sidModel_ = static_cast<SIDModel>(sidModeU8);
        setSIDModel(sidModel_);

        if (!rdr.readF64(sidCycleCounter)) return false;

        if (!rdr.readF64(hpPrevIn)) return false;
        if (!rdr.readF64(hpPrevOut)) return false;

        auto loadVoiceRuntime = [&](Voice& v) -> bool {
            double phase = 0.0;
            bool overflow = false;
            uint32_t lfsr = 0;
            uint8_t envStateU8 = 0;
            double envLevel = 0.0;

            if (!rdr.readF64(phase)) return false;
            if (!rdr.readBool(overflow)) return false;
            if (!rdr.readU32(lfsr)) return false;
            if (!rdr.readU8(envStateU8)) return false;
            if (!rdr.readF64(envLevel)) return false;

            v.getOscillator().setPhase(phase);
            v.getOscillator().setPhaseOverflow(overflow);
            v.getOscillator().setNoiseLFSR(lfsr);

            v.getEnvelope().setState(static_cast<Envelope::State>(envStateU8));
            v.getEnvelope().setLevel(envLevel);
            return true;
        };

        if (!loadVoiceRuntime(voice1)) return false;
        if (!loadVoiceRuntime(voice2)) return false;
        if (!loadVoiceRuntime(voice3)) return false;

        double lp=0.0, bp=0.0, hp=0.0, dc=0.0;
        if (!rdr.readF64(lp)) return false;
        if (!rdr.readF64(bp)) return false;
        if (!rdr.readF64(hp)) return false;
        if (!rdr.readF64(dc)) return false;

        filterobj.setLowPassOut(lp);
        filterobj.setBandPassOut(bp);
        filterobj.setHighPassOut(hp);
        filterobj.setDcBlock(dc);

        rdr.exitChunkPayload(chunk);
        return true;
    }

    // Unknown chunk tag for SID
    return false;
}

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

void SID::setSIDModel(SIDModel model)
{
    sidModel_ = model;

    voice1.setSIDModel(model);
    voice2.setSIDModel(model);
    voice3.setSIDModel(model);

    filterobj.setModel(model);
    updateCutoffFromRegisters();
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
        case 0xD400: return sidRegisters.voice1.frequencyLow;
        case 0xD401: return sidRegisters.voice1.frequencyHigh;
        case 0xD402: return sidRegisters.voice1.pulseWidthLow;
        case 0xD403: return sidRegisters.voice1.pulseWidthHigh;
        case 0xD404: return sidRegisters.voice1.control;
        case 0xD405: return sidRegisters.voice1.attackDecay;
        case 0xD406: return sidRegisters.voice1.sustainRelease;
        case 0xD407: return sidRegisters.voice2.frequencyLow;
        case 0xD408: return sidRegisters.voice2.frequencyHigh;
        case 0xD409: return sidRegisters.voice2.pulseWidthLow;
        case 0xD40A: return sidRegisters.voice2.pulseWidthHigh;
        case 0xD40B: return sidRegisters.voice2.control;
        case 0xD40C: return sidRegisters.voice2.attackDecay;
        case 0xD40D: return sidRegisters.voice2.sustainRelease;
        case 0xD40E: return sidRegisters.voice3.frequencyLow;
        case 0xD40F: return sidRegisters.voice3.frequencyHigh;
        case 0xD410: return sidRegisters.voice3.pulseWidthLow;
        case 0xD411: return sidRegisters.voice3.pulseWidthHigh;
        case 0xD412: return sidRegisters.voice3.control;
        case 0xD413: return sidRegisters.voice3.attackDecay;
        case 0xD414: return sidRegisters.voice3.sustainRelease;
        case 0xD415: return sidRegisters.filter.cutoffLow;
        case 0xD416: return sidRegisters.filter.cutoffHigh;
        case 0xD417: return sidRegisters.filter.resonanceControl;
        case 0xD418: return sidRegisters.filter.volume;
        case 0xD419: return rand() & 0xFF; // read only register should return joy1 value; using random instead
        case 0xD41A: return rand() & 0xFF; // read only register should return joy2 value; using random instead
        case 0xD41B:
        {
            // OSC3: read current voice 3 oscillator output.
            return voice3.getOscillator().readOutput8();
        }
        case 0xD41C:
        {
            // ENV3: read current voice 3 envelope output.
            return voice3.getEnvelope().readOutput8();
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
            const uint8_t oldControl = sidRegisters.voice1.control;
            sidRegisters.voice1.control = value;

            applyVoiceControl(voice1, oldControl, value);
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
            const uint8_t oldControl = sidRegisters.voice2.control;
            sidRegisters.voice2.control = value;

            applyVoiceControl(voice2, oldControl, value);
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
            const uint8_t oldControl = sidRegisters.voice3.control;
            sidRegisters.voice3.control = value;

            applyVoiceControl(voice3, oldControl, value);
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
            // $D417 = RES/FILT
            //
            // Bits 0-2:
            //   bit 0 = voice 1 routed through filter
            //   bit 1 = voice 2 routed through filter
            //   bit 2 = voice 3 routed through filter
            //
            // Bit 3:
            //   external input routed through filter - not implemented yet
            //
            // Bits 4-7:
            //   resonance
            sidRegisters.filter.resonanceControl = value;
            filterobj.setResonance(value);
            break;
        }
        case 0xD418:
        {
            // $D418 = MODE/VOL
            //
            // Bits 4-6:
            //   bit 4 = low-pass
            //   bit 5 = band-pass
            //   bit 6 = high-pass
            //
            // Bit 7:
            //   voice 3 direct-path off
            //
            // Bits 0-3:
            //   master volume
            sidRegisters.filter.volume = value;

            const uint8_t filterMode = (value >> 4) & 0x07;
            filterobj.setMode(filterMode);
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
    const AnalogProfile profile = getAnalogProfile();

    const uint8_t resFilt = sidRegisters.filter.resonanceControl;
    const uint8_t modeVol = sidRegisters.filter.volume;

    // $D417 bits 0-2 select which voices enter the filter.
    const uint8_t filterRouteBits = resFilt & 0x07;

    // $D418 bits 4-6 select LP/BP/HP output modes.
    const uint8_t filterMode = (modeVol >> 4) & 0x07;

    // $D418 bit 7 disconnects voice 3 from the direct audio path.
    const bool voice3DirectOff = (modeVol & 0x80) != 0;

    filterobj.setMode(filterMode);

    constexpr double PER_VOICE_GAIN = 0.5;

    double filteredMixRaw = 0.0;
    double unfilteredMixRaw = 0.0;
    bool anyFilteredVoice = false;

    for (int i = 0; i < 3; ++i)
    {
        Voice* v = (i == 0) ? &voice1 : (i == 1) ? &voice2 : &voice3;

        const bool routedToFilter = (filterRouteBits & (1 << i)) != 0;
        v->setFilterRouted(routedToFilter);

        const double s = v->generateVoiceSample() * PER_VOICE_GAIN;

        if (routedToFilter)
        {
            filteredMixRaw += s;
            anyFilteredVoice = true;
        }
        else
        {
            if (!(i == 2 && voice3DirectOff))
                unfilteredMixRaw += s;
        }
    }

    filteredMixRaw = std::clamp(filteredMixRaw, -1.0, 1.0);
    unfilteredMixRaw = std::clamp(unfilteredMixRaw, -1.0, 1.0);

    const double filteredMix =
        filteredMixRaw * profile.filterInputGain;

    const double unfilteredMix =
        unfilteredMixRaw * profile.directGain;

    double filteredOut = 0.0;

    if (anyFilteredVoice)
    {
        const double filterResult = filterobj.processSample(filteredMix);

        if (filterMode != 0)
            filteredOut = filterResult * profile.filterOutputGain;
    }

    double mixed = filteredOut + unfilteredMix;

    const uint8_t volumeNibble = modeVol & 0x0F;
    const double masterVol = static_cast<double>(volumeNibble) / 15.0;

    mixed *= masterVol;

    // $D418 volume DAC behavior differs strongly between 6581 and 8580.
    const double volumeDacCentered =
        (static_cast<double>(volumeNibble) - 7.5) / 7.5;

    mixed += volumeDacCentered * profile.volumeDacGain;

    // 6581 has more analog bias / character than 8580.
    mixed += profile.outputBias;

    // Model-specific analog saturation.
    mixed *= profile.softClipDrive;
    mixed = std::clamp(mixed, -1.5, 1.5);
    mixed = mixed / (1.0 + std::abs(mixed));

    const double hp = HP_ALPHA * (hpPrevOut + mixed - hpPrevIn);
    hpPrevIn  = mixed;
    hpPrevOut = hp;

    return hp;
}

void SID::tick(uint32_t cycles)
{
    voice1.clockEnvelope(static_cast<double>(cycles));
    voice2.clockEnvelope(static_cast<double>(cycles));
    voice3.clockEnvelope(static_cast<double>(cycles));

    sidCycleCounter += static_cast<double>(cycles);

    if (sidCyclesPerAudioSample <= 0.0)
        return;

    size_t samplesToPush = static_cast<size_t>(sidCycleCounter / sidCyclesPerAudioSample);
    sidCycleCounter -= samplesToPush * sidCyclesPerAudioSample;

    for (size_t i = 0; i < samplesToPush; ++i)
    {
        double sample = generateAudioSample();
        audioBuf.push(sample);

        audioGeneratedSamples.fetch_add(1, std::memory_order_relaxed);
        audioBufferedSamples.fetch_add(1, std::memory_order_relaxed);
    }
}

double SID::popSample()
{
    constexpr int RECOVERY_LEN = 64;

    audioConsumedSamples.fetch_add(1, std::memory_order_relaxed);

    double s = 0.0;

    if (audioBuf.pop(s))
    {
        int buffered = audioBufferedSamples.load(std::memory_order_relaxed);
        if (buffered > 0)
            audioBufferedSamples.fetch_sub(1, std::memory_order_relaxed);

        if (audioWasUnderrunning)
        {
            audioWasUnderrunning = false;
            underrunRecoverySamples = RECOVERY_LEN;
            recoveryStartSample = underrunOutputSample;
        }

        if (underrunRecoverySamples > 0)
        {
            const double t =
                1.0 - (static_cast<double>(underrunRecoverySamples) /
                       static_cast<double>(RECOVERY_LEN));

            const double out = recoveryStartSample + (s - recoveryStartSample) * t;

            --underrunRecoverySamples;
            lastOutputSample = out;
            underrunOutputSample = out;
            return out;
        }

        lastOutputSample = s;
        underrunOutputSample = s;
        return s;
    }

    audioUnderrunCount.fetch_add(1, std::memory_order_relaxed);

    audioWasUnderrunning = true;

    // Fade toward silence during starvation.
    underrunOutputSample *= 0.995;

    lastOutputSample = underrunOutputSample;
    return underrunOutputSample;
}

void SID::reset()
{
    std::memset(&sidRegisters, 0, sizeof(sidRegisters));
    sidRegisters.filter.volume = 0x00;
    sidCycleCounter = 0.0;

    lastOutputSample = 0.0;
    underrunOutputSample = 0.0;
    recoveryStartSample = 0.0;
    audioUnderrunCount = 0;
    audioWasUnderrunning = false;
    underrunRecoverySamples = 0;

    voice1.reset();
    voice2.reset();
    voice3.reset();

    configureOscillatorSources();

    filterobj.reset();
    setSIDModel(sidModel_);

    hpPrevIn = 0.0;
    hpPrevOut = 0.0;

    audioGeneratedSamples.store(0, std::memory_order_relaxed);
    audioConsumedSamples.store(0, std::memory_order_relaxed);
    audioUnderrunCount.store(0, std::memory_order_relaxed);
    audioBufferedSamples.store(0, std::memory_order_relaxed);
}

SID::AnalogProfile SID::getAnalogProfile() const
{
    const SIDModelProfile& profile = getSIDModelProfile(sidModel_);

    return AnalogProfile
    {
        profile.directGain,
        profile.filterInputGain,
        profile.filterOutputGain,
        profile.volumeDacGain,
        profile.outputBias,
        profile.softClipDrive
    };
}

uint16_t SID::combineBytes(uint8_t high, uint8_t low)
{
    return (static_cast<uint16_t>(high) << 8) | low;
}

void SID::updateEnvelopeParameters(Voice& voice, voiceRegisters& regs)
{
    const uint8_t attackIdx  = (regs.attackDecay >> 4) & 0x0F;
    const uint8_t decayIdx   =  regs.attackDecay       & 0x0F;
    const uint8_t sustainIdx = (regs.sustainRelease >> 4) & 0x0F;
    const uint8_t releaseIdx =  regs.sustainRelease       & 0x0F;

    voice.setADSR(attackIdx, decayIdx, sustainIdx, releaseIdx);
}

void SID::updateCutoffFromRegisters()
{
    const uint16_t cutoff11bit =
        (static_cast<uint16_t>(sidRegisters.filter.cutoffHigh) << 3) |
        (sidRegisters.filter.cutoffLow & 0x07);

    const double cutoffFreq =
        mapSIDCutoff11BitToHzTable(cutoff11bit, sidModel_);

    filterobj.setCutoffFreq(cutoffFreq);
}

void SID::applyVoiceControl(Voice& voice, uint8_t oldControl, uint8_t newControl)
{
    voice.setControl(newControl);

    const bool oldGate = (oldControl & 0x01) != 0;
    const bool newGate = (newControl & 0x01) != 0;

    if (!oldGate && newGate)
    {
        voice.trigger();
    }
    else if (oldGate && !newGate)
    {
        voice.release();
    }
}

void SID::configureOscillatorSources()
{
    // SID oscillator source chain:
    //
    // Voice 1 SYNC/RING source = voice 3
    // Voice 2 SYNC/RING source = voice 1
    // Voice 3 SYNC/RING source = voice 2

    voice1.getOscillator().setSyncSource(&voice3.getOscillator());
    voice1.getOscillator().setRingSource(&voice3.getOscillator());

    voice2.getOscillator().setSyncSource(&voice1.getOscillator());
    voice2.getOscillator().setRingSource(&voice1.getOscillator());

    voice3.getOscillator().setSyncSource(&voice2.getOscillator());
    voice3.getOscillator().setRingSource(&voice2.getOscillator());
}

std::string SID::dumpRegisters(const std::string& group)
{
    std::stringstream out;
    out << std::hex << std::uppercase << std::setfill('0');

    if (group == "voice1" || group == "voices" || group == "all")
        out << dumpVoice(sidRegisters.voice1, voice1, 1);

    if (group == "voice2" || group == "voices" || group == "all")
        out << dumpVoice(sidRegisters.voice2, voice2, 2);

    if (group == "voice3" || group == "voices" || group == "all")
        out << dumpVoice(sidRegisters.voice3, voice3, 3);

    if (group == "filter" || group == "all")
    {
        const uint16_t cutoff11bit =
            (static_cast<uint16_t>(sidRegisters.filter.cutoffHigh) << 3) |
            (sidRegisters.filter.cutoffLow & 0x07);

        const double cutoffNorm =
            static_cast<double>(cutoff11bit) / 2047.0;

        const double cutoffHz =
            mapSIDCutoff11BitToHzTable(cutoff11bit, sidModel_);

        const SIDModelProfile& profile =
            getSIDModelProfile(sidModel_);

        const uint8_t resRoute = sidRegisters.filter.resonanceControl;
        const uint8_t modeVol  = sidRegisters.filter.volume;

        const uint8_t resonanceNibble = (resRoute >> 4) & 0x0F;
        const uint8_t routeBits       = resRoute & 0x0F;

        const bool routeVoice1 = (resRoute & 0x01) != 0;
        const bool routeVoice2 = (resRoute & 0x02) != 0;
        const bool routeVoice3 = (resRoute & 0x04) != 0;
        const bool routeExtIn  = (resRoute & 0x08) != 0;

        const bool modeLowPass  = (modeVol & 0x10) != 0;
        const bool modeBandPass = (modeVol & 0x20) != 0;
        const bool modeHighPass = (modeVol & 0x40) != 0;

        const bool voice3DirectOff = (modeVol & 0x80) != 0;
        const uint8_t volumeNibble = modeVol & 0x0F;

        out << "Filter:\n";
        out << "  Model:          " << sidModelToString(sidModel_) << "\n";

        out << "  Cutoff raw:     $" << std::hex << std::setw(4) << std::setfill('0')
            << cutoff11bit
            << std::dec << " (" << cutoff11bit << "/2047)\n";

        out << "  Cutoff norm:    " << std::fixed << std::setprecision(3)
            << cutoffNorm << "\n";

        out << "  Cutoff mapped:  " << std::fixed << std::setprecision(1)
            << cutoffHz << " Hz\n";

        out << "\n";
        out << "  Profile:\n";

        out << "    Filter drive:      " << std::fixed << std::setprecision(3)
            << profile.filterDrive << "\n";

        out << "    Filter asymmetry:  " << std::fixed << std::setprecision(3)
            << profile.filterAsymmetry << "\n";

        out << "    Resonance curve:   " << std::fixed << std::setprecision(3)
            << profile.resonanceCurvePower << "\n";

        out << "    Cutoff range:      " << std::fixed << std::setprecision(1)
            << profile.cutoffMinHz << " - " << profile.cutoffMaxHz << " Hz\n";

        out << "\n";
        out << "  RES/FILT=$" << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(resRoute) << std::dec << "\n";

        out << "    Resonance:    $" << std::hex << static_cast<int>(resonanceNibble)
            << std::dec << " (" << static_cast<int>(resonanceNibble) << "/15)\n";

        out << "    Routing:      "
            << "V1=" << (routeVoice1 ? "Y" : "N") << "  "
            << "V2=" << (routeVoice2 ? "Y" : "N") << "  "
            << "V3=" << (routeVoice3 ? "Y" : "N") << "  "
            << "EXT=" << (routeExtIn ? "Y" : "N")
            << "  bits=$" << std::hex << static_cast<int>(routeBits) << std::dec << "\n";

        out << "  MODE/VOL=$" << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(modeVol) << std::dec << "\n";

        out << "    Mode:         "
            << "LP=" << (modeLowPass ? "Y" : "N") << "  "
            << "BP=" << (modeBandPass ? "Y" : "N") << "  "
            << "HP=" << (modeHighPass ? "Y" : "N") << "\n";

        out << "    Voice3 off:   " << (voice3DirectOff ? "Y" : "N") << "\n";

        out << "    Volume:       $" << std::hex << static_cast<int>(volumeNibble)
            << std::dec << " (" << static_cast<int>(volumeNibble) << "/15)\n";
    }

    return out.str();
}

std::string SID::decodeControlRegister(uint8_t ctrl) const
{
    std::stringstream out;
    out << "  CTRL=$" << std::hex << std::setw(2) << static_cast<int>(ctrl) << " (";
    if (ctrl & 0x80) out << "NOISE ";
    if (ctrl & 0x40) out << "PULSE ";
    if (ctrl & 0x20) out << "SAW ";
    if (ctrl & 0x10) out << "TRI ";
    if (ctrl & 0x08) out << "TEST ";
    if (ctrl & 0x04) out << "RING ";
    if (ctrl & 0x02) out << "SYNC ";
    if (ctrl & 0x01) out << "GATE ";
    out << ")\n";
    return out.str();
}

std::string SID::decodeADSR(const voiceRegisters& regs, const Voice& voice, int index) const
{
    std::stringstream out;

    const uint8_t A = (regs.attackDecay >> 4) & 0x0F;
    const uint8_t D =  regs.attackDecay       & 0x0F;
    const uint8_t S = (regs.sustainRelease >> 4) & 0x0F;
    const uint8_t R =  regs.sustainRelease       & 0x0F;

    out << "  ADSR: A=$" << std::hex << static_cast<int>(A)
        << " D=$" << static_cast<int>(D)
        << " S=$" << static_cast<int>(S)
        << " R=$" << static_cast<int>(R) << "\n";

    const Envelope& env = voice.getEnvelope();

    out << "  ENV State=" << Envelope::stateToString(env.getState())
        << " Counter=$" << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(env.readOutput8())
        << std::dec
        << " Level=" << std::fixed << std::setprecision(3)
        << env.getLevel() << "\n";

    out << env.dumpDebug();

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

std::string SID::dumpAudioStats() const
{
    const uint64_t generated =
        audioGeneratedSamples.load(std::memory_order_relaxed);

    const uint64_t consumed =
        audioConsumedSamples.load(std::memory_order_relaxed);

    const uint64_t underruns =
        audioUnderrunCount.load(std::memory_order_relaxed);

    const int buffered =
        audioBufferedSamples.load(std::memory_order_relaxed);

    const uint64_t deficit =
        (consumed > generated) ? (consumed - generated) : 0;

    const uint64_t surplus =
        (generated > consumed) ? (generated - consumed) : 0;

    const double sr = (sampleRate > 0.0) ? sampleRate : 44100.0;

    const double bufferedMs =
        (static_cast<double>(buffered) / sr) * 1000.0;

    const double deficitMs =
        (static_cast<double>(deficit) / sr) * 1000.0;

    const double surplusMs =
        (static_cast<double>(surplus) / sr) * 1000.0;

    std::ostringstream out;

    out << "SID Audio:\n";
    out << "  Generated samples:   " << generated << "\n";
    out << "  Consumed samples:    " << consumed << "\n";
    out << "  Underruns:           " << underruns << "\n";
    out << "  Buffered samples:    " << buffered << "\n";
    out << "  Estimated depth:     " << surplus << "\n";

    out << std::fixed << std::setprecision(3);
    out << "  Buffered time:       " << bufferedMs << " ms\n";
    out << "  Deficit time:        " << deficitMs << " ms\n";
    out << "  Surplus time:        " << surplusMs << " ms\n";

    out << std::setprecision(6);
    out << "  Last output sample:  " << lastOutputSample << "\n";

    out << "\nHealth:\n";

    if (underruns == 0 && buffered >= 2048)
    {
        out << "  Status: OK - no underruns recorded; audio cushion is healthy.\n";
    }
    else if (underruns == 0 && buffered > 0)
    {
        out << "  Status: OK - no underruns recorded; audio cushion is present but small.\n";
    }
    else if (underruns == 0)
    {
        out << "  Status: NO CUSHION - no underruns yet, but queue is empty.\n";
    }
    else if (buffered >= 2048)
    {
        out << "  Status: RECOVERED - underruns occurred earlier, but cushion is now healthy.\n";
    }
    else if (buffered > 0)
    {
        out << "  Status: LOW - underruns occurred, but audio is currently buffered.\n";
    }
    else if (deficit < 5000)
    {
        out << "  Status: NO CUSHION - queue is empty, but generation is close to real time.\n";
    }
    else
    {
        out << "  Status: STARVING - SDL is consuming faster than SID is generating.\n";
    }

    return out.str();
}

std::string SID::dumpCutoffTable() const
{
    std::ostringstream out;

    out << "SID Cutoff Table Preview:\n";
    out << "  Model: " << sidModelToString(sidModel_) << "\n\n";
    out << "  Raw    Norm    Hz\n";
    out << "  ----   -----   --------\n";

    for (uint16_t raw = 0; raw <= 0x07FF; raw += 0x0100)
    {
        const double norm = static_cast<double>(raw) / 2047.0;
        const double hz = mapSIDCutoff11BitToHzTable(raw, sidModel_);

        out << "  $" << std::hex << std::uppercase << std::setw(4)
            << std::setfill('0') << raw
            << std::dec << std::setfill(' ')
            << "   " << std::fixed << std::setprecision(3) << norm
            << "   " << std::setw(8) << std::setprecision(1) << hz
            << "\n";
    }

    const uint16_t raw = 0x07FF;
    const double norm = 1.0;
    const double hz = mapSIDCutoff11BitToHzTable(raw, sidModel_);

    out << "  $" << std::hex << std::uppercase << std::setw(4)
        << std::setfill('0') << raw
        << std::dec << std::setfill(' ')
        << "   " << std::fixed << std::setprecision(3) << norm
        << "   " << std::setw(8) << std::setprecision(1) << hz
        << "\n";

    return out.str();
}

void SID::resetAudioStats()
{
    // Preserve current queue depth instead of blindly zeroing it.
    // audioBufferedSamples is current state, not just a stat counter.
    const int buffered = audioBufferedSamples.load(std::memory_order_relaxed);

    audioGeneratedSamples.store(static_cast<uint64_t>(std::max(0, buffered)),
                                std::memory_order_relaxed);
    audioConsumedSamples.store(0, std::memory_order_relaxed);
    audioUnderrunCount.store(0, std::memory_order_relaxed);

    // Do not reset lastOutputSample / underrun smoothing here.
    // This command should reset counters, not create an audible discontinuity.
    audioWasUnderrunning = false;
    underrunRecoverySamples = 0;
}
