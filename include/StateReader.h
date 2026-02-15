// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef STATEREADER_H
#define STATEREADER_H

#include <cstdint>
#include <fstream>
#include <cstring>
#include <vector>

class StateReader
{
    public:
        StateReader();
        virtual ~StateReader();

        bool loadFromFile(const std::string& path);
        bool loadFromMemory(std::vector<uint8_t> bytes);

        // Header
        bool readFileHeader(); // validates "C64S" and reads version
        uint32_t version() const { return fileVersion; }

        // Primitive reads (little-endian)
        bool readU8(uint8_t& out);
        bool readU16(uint16_t& out);
        bool readU32(uint32_t& out);
        bool readI32(int32_t& out);
        bool readVectorU8(std::vector<uint8_t>& out);
        bool readBool(bool& out);
        bool readBytes(void* dst, size_t len);

        // Chunk reading
        struct Chunk
        {
            char tag[4];
            uint32_t length = 0;
            size_t payloadOffset = 0; // offset into buffer
        };

        bool nextChunk(Chunk& out);         // reads next chunk header, positions at payload start
        void enterChunkPayload(const Chunk& c); // sets cursor to payload start
        void skipChunk(const Chunk& c);     // jumps cursor to end of this chunk

        size_t cursor() const { return pos; }
        size_t size() const { return buffer.size(); }

    protected:

    private:
        std::vector<uint8_t> buffer;
        size_t pos;
        uint32_t fileVersion;

        bool ensure(size_t bytes) const;
};

#endif // STATEREADER_H
