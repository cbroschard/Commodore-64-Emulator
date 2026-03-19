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
    readDummyPending(false),
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
    readDummyPending    = false;
}


void SerialEEPROM93C86::save(StateWriter& wrtr) const
{
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

    wrtr.writeBool(readDummyPending);
}

bool SerialEEPROM93C86::load(StateReader& rdr)
{
    for (auto& b : data)
    {
        if (!rdr.readU8(b))
        {
            return false;
        }
    }

    if (!rdr.readBool(cs))                  return false;
    if (!rdr.readBool(clk))                 return false;
    if (!rdr.readBool(di))                  return false;
    if (!rdr.readBool(dout))                return false;

    if (!rdr.readU32(shiftReg))             return false;
    if (!rdr.readU32(bitCount))             return false;
    if (!rdr.readBool(prevClk))             return false;

    uint32_t cmd = 0;
    if (!rdr.readU32(cmd))                  return false;
    currentCmd = static_cast<Command>(cmd);

    if (!rdr.readU16(currentAddress))       return false;
    if (!rdr.readU16(outShiftReg))          return false;
    if (!rdr.readU32(outBitCount))          return false;
    if (!rdr.readBool(writeEnableLatch))    return false;
    if (!rdr.readBool(commandLatched))      return false;

    if (!rdr.readBool(readDummyPending))    return false;

    return true;
}

bool SerialEEPROM93C86::saveRaw(std::ostream& out) const
{
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    return out.good();
}

bool SerialEEPROM93C86::loadRaw(std::istream& in)
{
    std::fill(data.begin(), data.end(), 0xFF);

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
        readDummyPending = false;
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

    readDummyPending = false;
}

void SerialEEPROM93C86::handleRisingEdge()
{
    // If we're currently shifting read data out, drive DO first
    if (currentCmd == Command::Read && commandLatched)
    {
        if (readDummyPending)
        {
            dout = false;
            readDummyPending = false;
            return;
        }

        if (outBitCount == 0)
        {
            currentAddress = static_cast<uint16_t>((currentAddress + 1) & 0x07FF);
            prepareReadData();

            // sequential read continues immediately with data bits
            readDummyPending = false;
        }

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

    // command length = 1 start + 2 opcode + 11 address = 14 bits
    if (bitCount < 14)
        return;

    const uint32_t frame  = shiftReg & 0x3FFF;
    const uint8_t  start  = static_cast<uint8_t>((frame >> 13) & 0x01);
    const uint8_t  opcode = static_cast<uint8_t>((frame >> 11) & 0x03);
    const uint16_t addr   = static_cast<uint16_t>(frame & 0x07FF);

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
        {
            currentCmd = Command::Read;
            commandLatched = true;
            prepareReadData();
            shiftReg = 0;
            bitCount = 0;
            break;
        }
        case 0b01: // WRITE
        {
            currentCmd = Command::Write;
            commandLatched = true;
            shiftReg = 0;
            bitCount = 0;
            break;
        }
        case 0b00:
        {
            const uint16_t top2 = (addr >> 9) & 0x03;

            if (top2 == 0x03)
                writeEnableLatch = true;   // EWEN
            else if (top2 == 0x00)
                writeEnableLatch = false;  // EWDS

            resetTransaction();
            break;
        }
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
    readDummyPending = true;
    dout = false;
}

void SerialEEPROM93C86::commitWriteByte(uint8_t value)
{
    if (!writeEnableLatch)
        return;

    if (currentAddress < data.size() && data[currentAddress] != value)
        data[currentAddress] = value;
}
