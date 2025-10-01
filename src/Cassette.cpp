// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cassette.h"

Cassette::Cassette() :
    cassetteLoaded(false),
    playPressed(false),
    motorStatus(false),
    tapePosition(0),
    data(1),
    setLogging(false)
{

}

Cassette::~Cassette() = default;

void Cassette::startMotor()
{
    if (motorStatus) return;
    motorStatus = true;
}

bool Cassette::loadCassette(const std::string& path)
{
    tapeImage = createTapeImage(path);
    if (!tapeImage || !tapeImage->loadTape(path))
    {
        std::cerr << "Error: Unable to load tape!" << std::endl;
        return false;
    }
    cassetteLoaded = true;
    rewind();
    return true;
}

void Cassette::unloadCassette() {
    cassetteLoaded = false;
    playPressed = false;
    motorStatus = false;
    setData(true); // idle high
    if (mem) mem->setCassetteSenseLow(false);
    tapeImage.reset();
}

bool Cassette::isT64() const
{
    if (tapeImage) return tapeImage->isT64();
    return false;
}

void Cassette::play()
{
    playPressed = true;
    if (mem) mem->setCassetteSenseLow(true);
}

void Cassette::stop()
{
    playPressed = false;
    motorStatus = false;
    if (mem) mem->setCassetteSenseLow(false);
}

void Cassette::rewind()
{
    if (tapeImage) tapeImage->rewind();
    setData(true);  // idle-high after rewind
    tapePosition = 0;
}

void Cassette::eject()
{
    // Simply call our unload method
    unloadCassette();
}

void Cassette::tick()
{
    // Cassette must be loaded, play pressed, and motor running
    if (!cassetteLoaded || !motorStatus || !playPressed) {
        setData(true);  // idle high
        return;
    }

    if (!tapeImage) {
        setData(true);
        return;
    }

    // Advance one cycle of tape simulation
    tapeImage->simulateLoading();
    setData(tapeImage->currentBit());
}

T64LoadResult Cassette::t64LoadPrgIntoMemory()
{
    T64LoadResult result;
    T64* t64 = static_cast<T64*>(tapeImage.get());
    if (t64->isT64() && t64->hasLoadedFile() && mem)
    {
        result.prgStart = t64->getPrgStart();
        result.prgEnd = t64->getPrgEnd();

        // Update $AE and $AF with location
        mem->write(0xAE, result.prgStart & 0xFF);
        mem->write(0xAF, result.prgStart >> 8);

        uint16_t prgLen = result.prgEnd - result.prgStart + 1; // Determine the size
        const uint8_t* prgData = t64->getPrgData();
        for (size_t i = 0; i < prgLen; ++i)
        {
            mem->write(result.prgStart + i, prgData[i]);
        }
        if (result.prgStart == 0x0801)
        {
            uint16_t basicEnd = result.prgEnd + 1;
            // Update BASIC pointers
            mem->write16(TXTAB, result.prgStart); //start of BASIC text
            mem->write16(VARTAB, basicEnd); // start of variables
            mem->write16(ARYTAB, basicEnd); //start of arrays
            mem->write16(STREND, basicEnd); //end of strings
        }
    }
    else
    {
        result.success = false;
        return result;
    }
    result.success = true;
    return result;
}

std::string Cassette::dumpPulses(size_t count) const
{
    std::ostringstream out;

    if (!tapeImage) {
        out << "No tape image loaded.\n";
        return out.str();
    }

    out << "Tape version: " << static_cast<int>(tapeImage->debugTapeVersion()) << "\n";
    out << "Current pulse index: " << tapeImage->debugPulseIndex()
        << " / " << tapeImage->debugPulseCount() << "\n";
    out << "Pulse Remaining: " << tapeImage->debugPulseRemaining() << "\n";
    out << "Current level: " << (getData() ? "High" : "Low") << "\n";

    for (size_t i = 0; i < count; i++)
    {
        uint32_t dur = tapeImage->debugNextPulse(i);
        if (dur == 0) break;
        out << " +" << i << ": " << dur << " cycles";
        if (dur > 1000000) out << " (gap)";
        out << "\n";
    }
    return out.str();
}
