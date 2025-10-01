// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Tape/TAP.h"

TAP::TAP() :
    pulseIndex(0),
    pulseRemaining(0),
    currentLevel(true)
{

}

TAP::~TAP() = default;

void TAP::attachLoggingInstance(Logging* logger)
{
    this->logger = logger;
}

bool TAP::loadTape(const std::string& filePath, VideoMode mode)
{
    if (!loadFile(filePath, tapeData))
    {
        return false;
    }

    if (tapeData.size() < sizeof(header))
    {
        throw std::runtime_error("Error: File too small to contain a valid header.");
    }

    // Copy header bytes from tapeData into header
    std::memcpy(&header, tapeData.data(), sizeof(header));

    if (!validateHeader())
    {
        return false;
    }
    pulses = parsePulses(mode);
    if (pulses.empty())
    {
        return false;
    }
    pulseIndex = 0;
    pulseRemaining = pulses[0].duration;
    currentLevel = true;
    return true;
}

void TAP::rewind()
{
    // Start in idle high (Datasette read line idles high)
    currentLevel = true;

    // Go to the first pulse
    pulseIndex = 0;

    // Preload the first duration so we don't toggle immediately on the first tick
    if (!pulses.empty())
    {
        // If the first pulse has zero duration (bad/edge case), skip forward
        size_t i = 0;
        while (i < pulses.size() && pulses[i].duration == 0) ++i;

        pulseIndex = i;
        pulseRemaining = (pulseIndex < pulses.size()) ? pulses[pulseIndex].duration : 0;
    }
    else
    {
        // No pulses -> line stays idle high forever
        pulseRemaining = 0;
    }
}

void TAP::simulateLoading()
{
    if (pulseIndex >= pulses.size())
    {
        currentLevel = true; // idle high
        return;
    }

    // Consume one cycle
    if (pulseRemaining > 0)
    {
        pulseRemaining--;
        return; // still inside this pulse
    }

    // Pulse finished
    const auto &pulse = pulses[pulseIndex];

    if (pulse.isGap)
    {
        // Gap = silence, just stay high
        currentLevel = true;
    }
    else
    {
        // Normal pulse = toggle
        currentLevel = !currentLevel;
    }

    // Advance to next
    pulseIndex++;
    if (pulseIndex < pulses.size())
    {
        pulseRemaining = pulses[pulseIndex].duration;
    }
    else
    {
        currentLevel = true; // end of tape
    }
}

bool TAP::validateHeader()
{
    // First check the signature to ensure it's really a C16 or C64 tape file
    if ((std::strncmp(header.fileSignature, "C64-TAPE-RAW", sizeof(header.fileSignature)) !=0)
        && (std::strncmp(header.fileSignature, "C16-TAPE-RAW", sizeof(header.fileSignature)) !=0))
    {
        std::cerr << "Error: Tape file is not a C64 or C16 tape!" << std::endl;
        return false;
    }

    // Check version matches what is expected
    if (header.tapeVersion > 2)
    {
        std::cerr << "Error: The tape raw version is invalid!" << std::endl;
        return false;
    }

    // Validate that the data size in the header matches the actual file size.
    size_t expectedDataSize = tapeData.size() - sizeof(header);
    if (header.dataSize != expectedDataSize)
    {
        std::cerr << "Error: Declared data size (" << header.dataSize
                  << " bytes) does not match actual data size (" << expectedDataSize << " bytes)!" << std::endl;
        return false;
    }

    // Check the platform is inline with expected range
    if (header.platform > 5)
    {
        std::cerr << "Error: The platform is invalid!" << std::endl;
        return false;
    }

    // Check video standard is inline with expected range
    if (header.videoStandard > 3)
    {
        std::cerr << "Error: The video standard is invalid!" << std::endl;
        return false;
    }
    return true;
}

bool TAP::loadFile(const std::string& path, std::vector<uint8_t>& buffer)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        std::cerr << "Failed to open TAPE file: " << path << std::endl;
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    buffer.resize(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size))
    {
        std::cerr << "Failed to read TAPE file: " << path << std::endl;
        return false;
    }

    std::cout << "Loaded TAPE file: " << path << " (" << size << " bytes)" << std::endl;
    return true;
}

std::vector<TAP::tapePulse> TAP::parsePulses(VideoMode mode)
{
    std::vector<tapePulse> pulses;
    size_t pos = sizeof(header);

    // Determine native tape mode
    VideoMode tapeMode = (header.videoStandard == 1) ? VideoMode::NTSC : VideoMode::PAL;

    // PAL↔NTSC scaling if modes mismatch
    double scalingFactor = 1.0;
    if (tapeMode != mode) {
        scalingFactor = (mode == VideoMode::NTSC) ? (NTSC_CLOCK / PAL_CLOCK)
                                                  : (PAL_CLOCK / NTSC_CLOCK);
    }

    while (pos < tapeData.size())
    {
        uint32_t raw = 0;
        uint32_t duration = 0;

        if (header.tapeVersion == 0 || header.tapeVersion == 1)
        {
            if (tapeData[pos] != 0)
            {
                raw = tapeData[pos];
                duration = raw * 8; // units = 8 cycles
                pos += 1;
            }
            else
            {
                if (pos + 3 >= tapeData.size()) break;
                uint32_t lo  = tapeData[pos + 1];
                uint32_t mid = tapeData[pos + 2];
                uint32_t hi  = tapeData[pos + 3];
                raw = (lo | (mid << 8) | (hi << 16));
                duration = raw * 8; // still 8 cycles
                pos += 4;
            }
        }
        else if (header.tapeVersion == 2)
        {
            if (tapeData[pos] != 0)
            {
                raw = tapeData[pos];
                duration = raw; // already cycles
                pos += 1;
            }
            else
            {
                if (pos + 3 >= tapeData.size()) break;
                uint32_t lo  = tapeData[pos + 1];
                uint32_t mid = tapeData[pos + 2];
                uint32_t hi  = tapeData[pos + 3];
                raw = (lo | (mid << 8) | (hi << 16));
                duration = raw; // already cycles
                pos += 4;
            }
        }

        if (duration > 0)
        {
            uint32_t scaled = static_cast<uint32_t>(duration * scalingFactor);

            // Consider gaps only if extremely long
            bool isGap = (scaled > 1000000);

            pulses.push_back({scaled, isGap});
        }
    }

    return pulses;
}

bool TAP::currentBit() const
{
    return currentLevel;
}

uint32_t TAP::debugCurrentPulse() const
{
    if (pulseIndex < pulses.size()) return pulses[pulseIndex].duration;
    return 0;
}

uint32_t TAP::debugNextPulse(size_t lookahead) const
{
    size_t idx = pulseIndex + lookahead;
    if (idx < pulses.size()) return pulses[idx].duration;
    return 0;
}
