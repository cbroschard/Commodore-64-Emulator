// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "EEPROM/SerialEEPROM93C86.h"

SerialEEPROM93C86::SerialEEPROM93C86() :
    currentCmd(Command::None),
    currentAddress(0),
    outShiftReg(0),
    outBitCount(0),
    writeEnableLatch(false),
    commandLatched(false),
    cs(false),
    clk(false),
    di(false),
    dout(false),
    shiftReg(0),
    bitCount(0),
    prevClk(false)
{
    // Set to "erased" EEPROM on start
    std::fill(std::begin(data), std::end(data), 0xFF);
}

SerialEEPROM93C86::~SerialEEPROM93C86() = default;

void SerialEEPROM93C86::reset()
{

    // Reset all lines
    cs          = false;
    clk         = false;
    di          = false;
    dout        = false;

    // Reset protocol state
    currentCmd          = Command::None;
    currentAddress      = 0;
    outShiftReg         = 0;
    outBitCount         = 0;
    shiftReg            = 0;
    bitCount            = 0;
    writeEnableLatch    = false;
    commandLatched      = false;
    prevClk             = false;
}


void SerialEEPROM93C86::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("EE93");
    wrtr.writeU32(1); // version

    for (uint8_t b : data)
        wrtr.writeU8(b);

    wrtr.writeBool(cs);
    wrtr.writeBool(clk);
    wrtr.writeBool(di);
    wrtr.writeBool(dout);

    wrtr.writeU32(shiftReg);
    wrtr.writeU32(bitCount);
    wrtr.writeBool(prevClk);

    wrtr.writeU32(static_cast<uint32_t>(currentCmd));
    wrtr.writeU16(currentAddress);
    wrtr.writeU16(outShiftReg);
    wrtr.writeU32(outBitCount);
    wrtr.writeBool(writeEnableLatch);
    wrtr.writeBool(commandLatched);

    wrtr.endChunk();
}

bool SerialEEPROM93C86::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "EE93", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver)) { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)          { rdr.exitChunkPayload(chunk); return false; }

        for (auto& b : data)
        {
            if (!rdr.readU8(b))
            {
                rdr.exitChunkPayload(chunk);
                return false;
            }
        }

        if (!rdr.readBool(cs))               { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(clk))              { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(di))               { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(dout))             { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU32(shiftReg))          { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU32(bitCount))          { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(prevClk))          { rdr.exitChunkPayload(chunk); return false; }

        uint32_t cmd = 0;
        if (!rdr.readU32(cmd))               { rdr.exitChunkPayload(chunk); return false; }
        currentCmd = static_cast<Command>(cmd);

        if (!rdr.readU16(currentAddress))    { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU16(outShiftReg))       { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU32(outBitCount))       { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(writeEnableLatch)) { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(commandLatched))   { rdr.exitChunkPayload(chunk); return false; }

        rdr.exitChunkPayload(chunk);
        return true;
    }

    return false;
}

bool SerialEEPROM93C86::savePersistence(const std::string& path) const
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        return false;

    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));

    return out.good();
}

bool SerialEEPROM93C86::loadPersistence(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
        return false;

    in.read(reinterpret_cast<char*>(data.data()),
            static_cast<std::streamsize>(data.size()));

    if (!in.good() && !in.eof())
        return false;

    return true;
}

void SerialEEPROM93C86::setCS(bool level)
{
    if (cs == level)
        return;

    cs = level;

    if (!cs)
    {
        resetTransaction();
    }
    else
    {
        shiftReg = 0;
        bitCount = 0;
        currentCmd = Command::None;
        currentAddress = 0;
        outShiftReg = 0;
        outBitCount = 0;
        commandLatched = false;
        dout = false;
        prevClk = clk;
    }
}

void SerialEEPROM93C86::setCLK(bool level)
{
    if (clk == level)
        return;

    prevClk = clk;
    clk = level;

    if (!cs)
        return;

    if (!prevClk && clk)
        handleRisingEdge();
}

void SerialEEPROM93C86::setDI(bool level)
{
    di = level;
}

void SerialEEPROM93C86::resetTransaction()
{
    shiftReg = 0;
    bitCount = 0;
    prevClk = clk;

    currentCmd = Command::None;
    currentAddress = 0;
    outShiftReg = 0;
    outBitCount = 0;
    commandLatched = false;

    dout = false;
}

void SerialEEPROM93C86::handleRisingEdge()
{
    // If we're currently shifting read data out, drive DO first
    if (currentCmd == Command::Read && commandLatched && outBitCount > 0)
    {
        dout = ((outShiftReg & 0x8000) != 0);
        outShiftReg <<= 1;
        --outBitCount;
        return;
    }

    shiftReg = (shiftReg << 1) | (di ? 1u : 0u);
    ++bitCount;

    decodeCommandIfReady();

    if (currentCmd == Command::Write && commandLatched && bitCount >= 8)
    {
        commitWriteByte(static_cast<uint8_t>(shiftReg & 0xFF));
        resetTransaction();
    }
}

void SerialEEPROM93C86::decodeCommandIfReady()
{
    if (commandLatched)
        return;

    // [start:1][opcode:2][addr:8] = 11 bits
    if (bitCount < 11)
        return;

    const uint32_t frame = shiftReg & 0x7FF;
    const uint8_t start  = static_cast<uint8_t>((frame >> 10) & 0x01);
    const uint8_t opcode = static_cast<uint8_t>((frame >> 8) & 0x03);
    const uint8_t addr   = static_cast<uint8_t>(frame & 0xFF);

    if (start != 1)
    {
        shiftReg = 0;
        bitCount = 0;
        return;
    }

    currentAddress = addr;

    switch (opcode)
    {
        case 0b10: // READ
            currentCmd = Command::Read;
            commandLatched = true;
            prepareReadData();
            shiftReg = 0;
            bitCount = 0;
            break;

        case 0b01: // WRITE
            currentCmd = Command::Write;
            commandLatched = true;
            shiftReg = 0;
            bitCount = 0;
            break;

        case 0b00:
            // Simple first-pass EWEN / EWDS
            if ((addr & 0xC0) == 0xC0)
                writeEnableLatch = true;
            else if ((addr & 0xC0) == 0x00)
                writeEnableLatch = false;

            resetTransaction();
            break;

        default:
            resetTransaction();
            break;
    }
}

void SerialEEPROM93C86::prepareReadData()
{
    uint8_t value = 0xFF;

    if (currentAddress < data.size())
        value = data[currentAddress];

    outShiftReg = static_cast<uint16_t>(value) << 8;
    outBitCount = 8;

    dout = ((outShiftReg & 0x8000) != 0);
    outShiftReg <<= 1;
    --outBitCount;
}

void SerialEEPROM93C86::commitWriteByte(uint8_t value)
{
    if (!writeEnableLatch)
        return;

    if (currentAddress < data.size())
        data[currentAddress] = value;
}
