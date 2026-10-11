// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <algorithm>
#include <iostream>
#include "tape/DMP.h"

DMP::DMP() :
    pulseIndex(0),
    pulseRemaining(0),
    currentLevel(true),
    individualStates(false),
    recordingMode(false),
    blipWidth(1),
    blipCountdown(0),
    elapsedCycles(0),
    tapeTotalCycles(0)
{

}

DMP::~DMP() = default;

void DMP::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("DMP0");
    wrtr.writeU32(1); // Version

    // Current playback position
    wrtr.writeU32(static_cast<uint32_t>(pulseIndex));
    wrtr.writeU32(pulseRemaining);

    // Current READ signal state
    wrtr.writeBool(currentLevel);

    // Falling-edge pulse generation
    wrtr.writeU8(blipCountdown);
    wrtr.writeU8(blipWidth);

    // Tape progress
    wrtr.writeU64(elapsedCycles);

    wrtr.endChunk();
}

bool DMP::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "DMP0", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;

    if (!rdr.readU32(ver))                          { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                                   { rdr.exitChunkPayload(chunk); return false; }

    // Read playback position
    uint32_t idx = 0;
    uint32_t rem = 0;

    if (!rdr.readU32(idx))                          { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU32(rem))                          { rdr.exitChunkPayload(chunk); return false; }

    // Read current READ signal state
    bool level = true;

    if (!rdr.readBool(level))                       { rdr.exitChunkPayload(chunk); return false; }

    // Read falling-edge pulse state
    uint8_t blip = 0;
    uint8_t width = 1;

    if (!rdr.readU8(blip))                          { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(width))                         { rdr.exitChunkPayload(chunk); return false; }

    // Read elapsed tape cycles
    uint64_t elapsed = 0;

    if (!rdr.readU64(elapsed))                      { rdr.exitChunkPayload(chunk); return false; }

    // A valid DMP image must already be loaded
    if (pulses.empty())                             { rdr.exitChunkPayload(chunk); return false; }

    // Validate pulse index.
    // An index equal to pulses.size() indicates end of tape.
    if (static_cast<size_t>(idx) > pulses.size())   { rdr.exitChunkPayload(chunk); return false; }

    // Validate remaining pulse duration
    if (static_cast<size_t>(idx) < pulses.size())
    {
        if (rem > pulses[idx].duration)            { rdr.exitChunkPayload(chunk); return false; }
    }
    else
    {
        // No timing intervals remain.
        if (rem != 0)
        {
            rdr.exitChunkPayload(chunk);
            return false;
        }
    }

    // Validate blip width
    if (width == 0)
        width = 1;

    if (blip > width)                               { rdr.exitChunkPayload(chunk); return false; }

    // Validate elapsed position
    if (elapsed > tapeTotalCycles)                  { rdr.exitChunkPayload(chunk); return false; }

    // Restore playback state only after validation succeeds
    pulseIndex = static_cast<size_t>(idx);
    pulseRemaining = rem;

    currentLevel = level;

    blipCountdown = blip;
    blipWidth = width;

    elapsedCycles = elapsed;

    rdr.exitChunkPayload(chunk);

    return true;
}

bool DMP::loadTape(const std::string& filePath, VideoMode mode)
{
    tapeData.clear();
    pulses.clear();

    elapsedCycles = 0;
    tapeTotalCycles = 0;

    std::cout << "[DMP] Loading: " << filePath << std::endl;

    if (!loadFile(filePath, tapeData))
    {
        std::cerr << "[DMP] FAILED: loadFile()" << std::endl;
        return false;
    }

    std::cout << "[DMP] File size: "
              << tapeData.size() << " bytes" << std::endl;

    if (tapeData.size() < sizeof(header))
    {
        std::cerr << "[DMP] FAILED: File too small!" << std::endl;
        return false;
    }

    std::memcpy(&header, tapeData.data(), sizeof(header));

    std::cout << "[DMP] Header:"
              << " signature=" << std::string(header.fileSignature, 12)
              << " version=" << static_cast<int>(header.tapeVersion)
              << " platform=0x" << std::hex
              << static_cast<int>(header.platform) << std::dec
              << " video=" << static_cast<int>(header.videoStandard)
              << " bits=" << static_cast<int>(header.bitsPerSample)
              << " frequency=" << header.counterFrequency
              << std::endl;

    if (!validateHeader())
    {
        std::cerr << "[DMP] FAILED: validateHeader()" << std::endl;
        return false;
    }

    std::cout << "[DMP] Header validation passed" << std::endl;

    pulses = parsePulses(mode);

    if (pulses.empty())
    {
        std::cerr << "[DMP] FAILED: parsePulses() returned no pulses"
                  << std::endl;
        return false;
    }

    for (const auto& pulse : pulses)
        tapeTotalCycles += pulse.duration;

    rewind();

    std::cout << "[DMP] Loaded successfully!"
              << " Pulses=" << pulses.size()
              << " TotalCycles=" << tapeTotalCycles
              << " FirstPulse=" << pulseRemaining
              << std::endl;

    return true;
}

void DMP::rewind()
{
    blipCountdown = 0;
    currentLevel = true;

    elapsedCycles = 0;

    pulseIndex = 0;
    pulseRemaining = 0;

    if (pulses.empty())
        return;

    // Skip zero-duration pulses
    while (pulseIndex < pulses.size() &&
           pulses[pulseIndex].duration == 0)
    {
        ++pulseIndex;
    }

    if (pulseIndex < pulses.size())
        pulseRemaining = pulses[pulseIndex].duration;
}

void DMP::simulateLoading()
{
    // Allow active READ pulses to expire.
    if (blipCountdown > 0)
    {
        --blipCountdown;

        if (blipCountdown == 0)
            currentLevel = true;
    }

    if (pulseIndex >= pulses.size())
    {
        currentLevel = (blipCountdown == 0);
        return;
    }

    if (pulseRemaining > 0)
    {
        --pulseRemaining;
        ++elapsedCycles;

        if (pulseRemaining > 0)
            return;
    }

    // Complete-cycle DMP falling-edge event.
    currentLevel = false;
    blipCountdown = blipWidth;

    ++pulseIndex;

    if (pulseIndex < pulses.size())
        pulseRemaining = pulses[pulseIndex].duration;
    else
        pulseRemaining = 0;
}

bool DMP::currentBit() const
{
    return currentLevel;
}

uint64_t DMP::fastForwardCycles(uint64_t cyclesToSkip)
{
    if (pulses.empty() || pulseIndex >= pulses.size())
        return 0;

    uint64_t skippedCycles = 0;

    while (pulseIndex < pulses.size() && cyclesToSkip > 0)
    {
        if (pulseRemaining == 0)
        {
            ++pulseIndex;

            if (pulseIndex >= pulses.size())
                break;

            pulseRemaining = pulses[pulseIndex].duration;
            continue;
        }

        const uint64_t consume =
            std::min<uint64_t>(pulseRemaining, cyclesToSkip);

        pulseRemaining -= static_cast<uint32_t>(consume);
        cyclesToSkip -= consume;
        skippedCycles += consume;

        if (pulseRemaining == 0)
        {
            ++pulseIndex;

            if (pulseIndex < pulses.size())
                pulseRemaining = pulses[pulseIndex].duration;
        }
    }

    elapsedCycles += skippedCycles;

    // Fast-forward doesn't generate READ pulses.
    blipCountdown = 0;
    currentLevel = true;

    return skippedCycles;
}

uint64_t DMP::totalCycles() const
{
    return tapeTotalCycles;
}

uint64_t DMP::currentCycles() const
{
    return elapsedCycles;
}

bool DMP::atEnd() const
{
    return pulseIndex >= pulses.size() && blipCountdown == 0;
}

uint32_t DMP::debugCurrentPulse() const
{
    if (pulseIndex < pulses.size())
        return pulses[pulseIndex].duration;

    return 0;
}

uint32_t DMP::debugNextPulse(size_t lookahead) const
{
    if (pulseIndex >= pulses.size())
        return 0;

    if (lookahead >= pulses.size() - pulseIndex)
        return 0;

    return pulses[pulseIndex + lookahead].duration;
}

std::vector<DMP::tapePulse> DMP::parsePulses(VideoMode mode)
{
    std::vector<tapePulse> result;

    if (individualStates)
    {
        std::cerr << "Error: DMP individual-state decoding "
                  << "is not implemented yet!" << std::endl;
        return result;
    }

    const uint64_t cpuClock =
        (mode == VideoMode::NTSC)
            ? static_cast<uint64_t>(NTSC_CLOCK)
            : static_cast<uint64_t>(PAL_CLOCK);

    const uint64_t counterRate = header.counterFrequency;

    if (counterRate == 0)
        return result;

    const size_t dataStart = sizeof(header);

    uint64_t accumulatedTicks = 0;
    uint64_t totalTicks = 0;
    uint64_t previousCycles = 0;

    for (size_t pos = dataStart; pos + 1 < tapeData.size(); pos += 2)
    {
        // Read little-endian 16-bit sample
        const uint16_t sample =
            static_cast<uint16_t>(tapeData[pos]) |
            (static_cast<uint16_t>(tapeData[pos + 1]) << 8);

        accumulatedTicks += sample;

        // Overflow continuation
        if (sample == 0xFFFF)
            continue;

        totalTicks += accumulatedTicks;
        accumulatedTicks = 0;

        // Convert cumulative DMP time to CPU cycles.
        // This avoids cumulative per-sample rounding drift.
        const uint64_t convertedCycles = (totalTicks * cpuClock + counterRate / 2) / counterRate;

        const uint64_t duration = convertedCycles - previousCycles;

        previousCycles = convertedCycles;

        if (duration == 0)
            continue;

        // No truncated 32-bit durations.
        // Long intervals need to be represented by
        // a wider type or split into multiple events.
        if (duration > UINT32_MAX)
        {
            std::cerr << "Error: DMP interval too long!"
                      << std::endl;
            return {};
        }

        // Complete-cycle interval ending at a falling edge.
        result.push_back({
            static_cast<uint32_t>(duration),
            false
        });
    }

    // A trailing overflow sequence is incomplete.
    if (accumulatedTicks != 0)
    {
        std::cerr << "Error: Incomplete DMP timing sample!"
                  << std::endl;
        return {};
    }

    return result;
}

bool DMP::loadFile(const std::string& path, std::vector<uint8_t>& buffer)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);

    if (!file.is_open())
    {
        std::cerr << "Failed to open DMP file: " << path << std::endl;
        return false;
    }

    const std::streamsize size = file.tellg();

    if (size < static_cast<std::streamsize>(sizeof(header)))
    {
        std::cerr << "Error: DMP file is too small!" << std::endl;
        return false;
    }

    file.seekg(0, std::ios::beg);

    buffer.resize(static_cast<size_t>(size));

    if (!file.read(reinterpret_cast<char*>(buffer.data()), size))
    {
        std::cerr << "Failed to read DMP file: " << path << std::endl;
        buffer.clear();
        return false;
    }

    #ifdef Debug
    std::cout << "Loaded DMP file: " << path
              << " (" << size << " bytes)" << std::endl;
    #endif // Debug

    return true;
}

bool DMP::validateHeader()
{
    // Validate DMP signature
    if (std::memcmp(header.fileSignature, "DC2N-TAP-RAW", 12) != 0)
    {
        std::cerr << "Error: Invalid DMP file signature!"
                  << std::endl;
        return false;
    }

    // Validate DMP version
    if (header.tapeVersion > 1)
    {
        std::cerr << "Error: Unsupported DMP version!"
                  << std::endl;
        return false;
    }

    // Decode platform and recording flags
    individualStates = false;
    recordingMode = false;

    uint8_t machineID = header.platform;

    if (header.tapeVersion == 1)
    {
        machineID = header.platform & 0x0F;

        individualStates = (header.platform & 0x20) != 0;
        recordingMode = (header.platform & 0x10) != 0;

        // Reserved bits must be zero
        if ((header.platform & 0xC0) != 0)
        {
            std::cerr << "Error: Invalid DMP recording flags!"
                      << std::endl;
            return false;
        }
    }

    // C64 emulation only
    if (machineID != 0)
    {
        std::cerr << "Error: DMP recording is not for C64!"
                  << std::endl;
        return false;
    }

    // Validate video standard
    if (header.videoStandard > 1)
    {
        std::cerr << "Error: Unsupported DMP video standard!"
                  << std::endl;
        return false;
    }

    // DC2N uses 16-bit timing samples
    if (header.bitsPerSample != 16)
    {
        std::cerr << "Error: Unsupported DMP sample width!"
                  << std::endl;
        return false;
    }

    // Validate counter frequency
    if (header.counterFrequency == 0)
    {
        std::cerr << "Error: Invalid DMP counter frequency!"
                  << std::endl;
        return false;
    }

    // Timing data must contain complete 16-bit samples
    const size_t dataSize = tapeData.size() - sizeof(header);

    if ((dataSize % 2) != 0)
    {
        std::cerr << "Error: Invalid DMP timing data length!"
                  << std::endl;
        return false;
    }

    return true;
}
