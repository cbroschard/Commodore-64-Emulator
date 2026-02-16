// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "StateReader.h"

StateReader::StateReader() :
    pos(0),
    fileVersion(0)
{

}

StateReader::~StateReader() = default;

bool StateReader::loadFromFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    f.seekg(0, std::ios::end);
    const std::streamsize len = f.tellg();
    if (len <= 0) return false;
    f.seekg(0, std::ios::beg);

    std::vector<uint8_t> bytes(static_cast<size_t>(len));
    if (!f.read(reinterpret_cast<char*>(bytes.data()), len)) return false;

    return loadFromMemory(std::move(bytes));
}

bool StateReader::loadFromMemory(std::vector<uint8_t> bytes)
{
    buffer = std::move(bytes);
    pos = 0;
    fileVersion = 0;
    return true;
}

bool StateReader::ensure(size_t bytes) const
{
    return (pos + bytes) <= buffer.size();
}

bool StateReader::readFileHeader()
{
    if (!ensure(8)) return false;

    char magic[4];
    if (!readBytes(magic, 4)) return false;

    if (!(magic[0] == 'C' && magic[1] == '6' && magic[2] == '4' && magic[3] == 'S'))
        return false;

    uint32_t ver = 0;
    if (!readU32(ver)) return false;
    fileVersion = ver;
    return true;
}

bool StateReader::readU8(uint8_t& out)
{
    if (!ensure(1)) return false;
    out = buffer[pos++];
    return true;
}

bool StateReader::readU16(uint16_t& out)
{
    if (!ensure(2)) return false;
    const uint16_t b0 = buffer[pos + 0];
    const uint16_t b1 = buffer[pos + 1];
    out = static_cast<uint16_t>(b0 | (b1 << 8));
    pos += 2;
    return true;
}

bool StateReader::readU32(uint32_t& out)
{
    if (!ensure(4)) return false;
    const uint32_t b0 = buffer[pos + 0];
    const uint32_t b1 = buffer[pos + 1];
    const uint32_t b2 = buffer[pos + 2];
    const uint32_t b3 = buffer[pos + 3];
    out = (b0) | (b1 << 8) | (b2 << 16) | (b3 << 24);
    pos += 4;
    return true;
}

bool StateReader::readI32(int32_t& out)
{
    uint32_t tmp = 0;
    if (!readU32(tmp)) return false;
    out = static_cast<int32_t>(tmp);
    return true;
}

bool StateReader::readF64(double& out)
{
    static_assert(sizeof(double) == 8, "Double must be 8 bytes");

    uint32_t lo = 0;
    uint32_t hi = 0;

    if (!readU32(lo)) return false;
    if (!readU32(hi)) return false;

    uint64_t bits = static_cast<uint64_t>(lo)
                  | (static_cast<uint64_t>(hi) << 32);

    std::memcpy(&out, &bits, sizeof(out));
    return true;
}

bool StateReader::readString(std::string& out)
{
    uint32_t n = 0;
    if (!readU32(n)) return false;
    if (!ensure(n)) return false;

    out.clear();
    out.resize(static_cast<size_t>(n));
    if (n == 0) return true;

    return readBytes(out.data(), static_cast<size_t>(n));
}

bool StateReader::readVectorU8(std::vector<uint8_t>& out)
{
    uint32_t size = 0;
    if (!readU32(size)) return false;

    out.resize(static_cast<size_t>(size));
    if (size == 0) return true;

    return readBytes(out.data(), static_cast<size_t>(size));
}

bool StateReader::readVectorU16(std::vector<uint16_t>& out)
{
    uint32_t n = 0;
    if (!readU32(n)) return false;
    out.resize(static_cast<size_t>(n));
    for (uint32_t i = 0; i < n; ++i)
    {
        uint16_t v = 0;
        if (!readU16(v)) return false;
        out[i] = v;
    }
    return true;
}

bool StateReader::readBool(bool& out)
{
    uint8_t b = 0;
    if (!readU8(b)) return false;
    out = (b != 0);
    return true;
}

bool StateReader::readBytes(void* dst, size_t len)
{
    if (len == 0) return true;
    if (!dst) return false;
    if (!ensure(len)) return false;

    std::memcpy(dst, buffer.data() + pos, len);
    pos += len;
    return true;
}

bool StateReader::nextChunk(Chunk& out)
{
    // Need at least tag(4) + length(4)
    if (!ensure(8)) return false;

    out.tag[0] = static_cast<char>(buffer[pos + 0]);
    out.tag[1] = static_cast<char>(buffer[pos + 1]);
    out.tag[2] = static_cast<char>(buffer[pos + 2]);
    out.tag[3] = static_cast<char>(buffer[pos + 3]);
    pos += 4;

    uint32_t len = 0;
    if (!readU32(len)) return false;

    if (!ensure(len)) return false; // payload must exist
    out.length = len;
    out.payloadOffset = pos;        // payload begins here
    return true;
}

void StateReader::enterChunkPayload(const Chunk& c)
{
    pos = c.payloadOffset;
}

void StateReader::skipChunk(const Chunk& c)
{
    pos = c.payloadOffset + c.length;
}
