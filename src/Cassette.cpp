// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cassette.h"
#include "Memory.h"

Cassette::Cassette() :
    mem(nullptr),
    cassetteLoaded(false),
    playPressed(false),
    motorStatus(false),
    tapePosition(0),
    data(1),
    restoreTapePositionFromImage(false)
{

}

Cassette::~Cassette() noexcept
{
    try
    {
        stop();
        unloadCassette();
    }
    catch(...){}
}

void Cassette::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("CASS");
    wrtr.writeU32(2); // version

    wrtr.writeBool(cassetteLoaded);
    wrtr.writeBool(playPressed);
    wrtr.writeBool(motorStatus);
    wrtr.writeU8(data);
    wrtr.writeU64(static_cast<uint64_t>(tapePosition));

    wrtr.endChunk();

    if (tapeImage)
        tapeImage->saveState(wrtr);
}

bool Cassette::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "CASS", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;

        if (!rdr.readU32(ver))        { rdr.exitChunkPayload(chunk); return false; }

        if (ver < 1 || ver > 2)       { rdr.exitChunkPayload(chunk); return false; }

        bool loaded = false;
        bool play = false;
        bool motor = false;
        uint8_t outData = 1;

        if (!rdr.readBool(loaded))   { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(play))     { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(motor))    { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(outData))    { rdr.exitChunkPayload(chunk); return false; }

        uint64_t restoredTapePosition = 0;

        if (ver >= 2)
        {
            if (!rdr.readU64(restoredTapePosition)) {rdr.exitChunkPayload(chunk); return false; }

            restoreTapePositionFromImage = false;
        }
        else
        {
            // CASS version 1 did not store tapePosition.
            // Recover it after the TAP state chunk restores
            // pulseIndex and pulseRemaining.
            restoreTapePositionFromImage = true;
        }

        cassetteLoaded = loaded;
        playPressed    = play;
        motorStatus    = motor;
        data           = outData;

        if (ver >= 2)
            tapePosition = restoredTapePosition;
        else
            tapePosition = 0;

        if (mem)
            mem->setCassetteSenseLow(cassetteLoaded && playPressed);

        rdr.exitChunkPayload(chunk);
        return true;
    }

    if (tapeImage)
    {
        if (tapeImage->loadState(chunk, rdr))
        {
            if (restoreTapePositionFromImage)
            {
                tapePosition = tapeImage->currentCycles();
                restoreTapePositionFromImage = false;
            }

            return true;
        }
    }

    return false;
}

void Cassette::startMotor()
{
    if (motorStatus) return;
    motorStatus = true;
}

bool Cassette::loadCassette(const std::string& path, VideoMode mode)
{
    tapeImage = createTapeImage(path);
    if (!tapeImage || !tapeImage->loadTape(path, mode))
    {
        std::cerr << "Error: Unable to load tape!" << std::endl;
        return false;
    }
    cassetteLoaded = true;
    rewind();
    return true;
}

void Cassette::unloadCassette()
{
    cassetteLoaded = false;
    playPressed = false;
    motorStatus = false;
    tapePosition = 0;

    setData(true);

    if (mem)
        mem->setCassetteSenseLow(false);

    tapeImage.reset();
}

bool Cassette::isT64() const
{
    if (tapeImage) return tapeImage->isT64();
    return false;
}

void Cassette::play()
{
    if (!cassetteLoaded || !tapeImage)
        return;

    playPressed = true;

    if (mem)
        mem->setCassetteSenseLow(true);
}

void Cassette::stop()
{
    playPressed = false;

    if (mem)
        mem->setCassetteSenseLow(false);

    setData(true);
}

void Cassette::rewind()
{
    playPressed = false;

    if (mem)
        mem->setCassetteSenseLow(false);

    if (tapeImage)
        tapeImage->rewind();

    setData(true);
    tapePosition = 0;
}

void Cassette::fastForward()
{
    if (!tapeImage)
        return;

    playPressed = false;

    if (mem)
        mem->setCassetteSenseLow(false);

    constexpr uint64_t cyclesToSkip = 5000000;

    const uint64_t skippedCycles = tapeImage->fastForwardCycles(cyclesToSkip);

    tapePosition += skippedCycles;

    setData(true);

    if (tapeImage->atEnd())
        tapePosition = tapeImage->totalCycles();
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

    ++tapePosition;

    if (tapeImage->atEnd())
    {
        playPressed = false;

        if (mem)
            mem->setCassetteSenseLow(false);

        setData(true);
    }
}

T64LoadResult Cassette::t64LoadPrgIntoMemory()
{
    T64LoadResult result;

    // Must have a loaded T64 image and valid memory instance
    if (!tapeImage || !tapeImage->isT64() || !mem)
    {
        result.success = false;
        return result;
    }

    T64* t64 = static_cast<T64*>(tapeImage.get());

    if (!t64->hasLoadedFile())
    {
        result.success = false;
        return result;
    }

    result.prgStart = t64->getPrgStart();
    result.prgEnd   = t64->getPrgEnd();

    const uint8_t* prgData = t64->getPrgData();
    if (!prgData)
    {
        result.success = false;
        return result;
    }

    // Update $AE/$AF with load address
    mem->writeRAM(0xAE, result.prgEnd & 0xFF);
    mem->writeRAM(0xAF, result.prgEnd >> 8);

    const uint32_t prgLen = static_cast<uint32_t>(result.prgEnd) - static_cast<uint32_t>(result.prgStart);

    for (uint32_t i = 0; i < prgLen; ++i)
        mem->writeRAM(static_cast<uint16_t>(result.prgStart + i), prgData[i]);

    if (result.prgStart == 0x0801)
    {
        const uint16_t basicEnd = result.prgEnd;

        // Update BASIC pointers
        mem->write16(TXTAB,  result.prgStart);
        mem->write16(VARTAB, basicEnd);
        mem->write16(ARYTAB, basicEnd);
        mem->write16(STREND, basicEnd);
    }

    result.success = true;
    return result;
}

const std::vector<T64::T64Entry>& Cassette::getT64Entries() const
{
    static const std::vector<T64::T64Entry> empty;

    if (!tapeImage || !tapeImage->isT64())
        return empty;

    const T64* t64 = static_cast<const T64*>(tapeImage.get());
    return t64->getEntries();
}

bool Cassette::selectT64Entry(size_t index)
{
    if (!tapeImage || !tapeImage->isT64())
        return false;

    T64* t64 = static_cast<T64*>(tapeImage.get());
    return t64->selectEntry(index);
}

size_t Cassette::getSelectedT64Entry() const
{
    if (!tapeImage || !tapeImage->isT64())
        return 0;

    const T64* t64 = static_cast<const T64*>(tapeImage.get());
    return t64->getSelectedEntry();
}

uint64_t Cassette::getTotalTapeCycles() const
{
    return tapeImage ? tapeImage->totalCycles() : 0;
}

uint64_t Cassette::getCurrentTapeCycles() const
{
    return tapeImage ? tapeImage->currentCycles() : 0;
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

    out << "Tape position: " << tapePosition << " cycles\n";

    return out.str();
}
