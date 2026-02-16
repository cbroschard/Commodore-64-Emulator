// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "StateWriter.h"

StateWriter::StateWriter(uint32_t version) :
    fileVersion(version)
{

}

StateWriter::~StateWriter() = default;

void StateWriter::beginFile()
{
    // Magic
    const char magic[4] = { 'C','6','4','S' };
    writeBytes(magic, 4);
    // Version
    writeU32(fileVersion);
}

bool StateWriter::writeToFile(const std::string& path) const
{
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(buffer.data()),
            static_cast<std::streamsize>(buffer.size()));
    return static_cast<bool>(f);
}

void StateWriter::writeU8(uint8_t value)
{
    buffer.push_back(value);
}

void StateWriter::writeU16(uint16_t value)
{
    buffer.push_back(static_cast<uint8_t>(value & 0xFF));
    buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void StateWriter::writeU32(uint32_t value)
{
    buffer.push_back(static_cast<uint8_t>(value & 0xFF));
    buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    buffer.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    buffer.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

void StateWriter::writeI32(int32_t value)
{
    writeU32(static_cast<uint32_t>(value));
}

void StateWriter::writeF64(double value)
{
    static_assert(sizeof(double) == 8, "Double must be 8 bytes");

    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));

    // Write little-endian
    writeU32(static_cast<uint32_t>(bits & 0xFFFFFFFF));
    writeU32(static_cast<uint32_t>((bits >> 32) & 0xFFFFFFFF));
}

void StateWriter::writeString(const std::string& s)
{
    // Store as bytes (UTF-8)
    const auto* data = reinterpret_cast<const uint8_t*>(s.data());
    writeU32(static_cast<uint32_t>(s.size()));
    if (!s.empty())
        writeBytes(data, s.size());
}

void StateWriter::writeBool(bool value)
{
    writeU8(value ? 1 : 0);
}

void StateWriter::writeBytes(const void* ptr, size_t len)
{
    if (!ptr || len == 0) return;
    const auto* b = reinterpret_cast<const uint8_t*>(ptr);
    buffer.insert(buffer.end(), b, b + len);
}

void StateWriter::writeVectorU8(const std::vector<uint8_t>& value)
{
    writeU32(static_cast<uint32_t>(value.size()));
    if (!value.empty())
        writeBytes(value.data(), value.size());
}

void StateWriter::writeVectorU16(const std::vector<uint16_t>& value)
{
    writeU32(static_cast<uint32_t>(value.size()));
    for (uint16_t v : value)
        writeU16(v);
}

void StateWriter::writeFourCC(const char tag[4])
{
    writeBytes(tag, 4);
}

void StateWriter::patchU32(size_t offset, uint32_t value)
{
    // Patch little-endian u32 at offset
    buffer[offset + 0] = static_cast<uint8_t>(value & 0xFF);
    buffer[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    buffer[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    buffer[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

void StateWriter::beginChunk(const char tag[4])
{
    writeFourCC(tag);

    // Reserve length (u32) and remember where it is
    const size_t lengthOffset = buffer.size();
    writeU32(0);

    const size_t payloadStart = buffer.size();
    chunkStack.push_back(ChunkFrame{ lengthOffset, payloadStart });
}

void StateWriter::endChunk()
{
    if (chunkStack.empty())
        return;

    ChunkFrame frame = chunkStack.back();
    chunkStack.pop_back();

    const size_t payloadEnd = buffer.size();
    const uint32_t payloadLen = static_cast<uint32_t>(payloadEnd - frame.payloadStartOffset);

    patchU32(frame.lengthFieldOffset, payloadLen);
}
