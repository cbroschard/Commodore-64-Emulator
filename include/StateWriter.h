// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef STATEWRITER_H
#define STATEWRITER_H

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

class StateWriter
{
    public:
        explicit StateWriter(uint32_t version = 1);
        virtual ~StateWriter();

        // Header + finalize
        void beginFile();                  // writes "C64S" + version
        const std::vector<uint8_t>& data() const { return buffer; }
        bool writeToFile(const std::string& path) const;

        // Primitive writes
        void writeU8(uint8_t value);
        void writeU16(uint16_t value);
        void writeU32(uint32_t value);
        void writeI32(int32_t value);
        void writeF64(double value);
        void writeString(const std::string& s);
        void writeBool(bool value);

        void writeBytes(const void* ptr, size_t len);
        void writeVectorU8(const std::vector<uint8_t>& v);
        void writeVectorU16(const std::vector<uint16_t>& v);

        // Chunk writing
        void beginChunk(const char tag[4]); // writes tag + placeholder length
        void endChunk();

    protected:

    private:
         uint32_t fileVersion;
        std::vector<uint8_t> buffer;

        struct ChunkFrame
        {
            size_t lengthFieldOffset; // where the u32 length lives
            size_t payloadStartOffset; // start of payload bytes
        };
        std::vector<ChunkFrame> chunkStack;

        void writeFourCC(const char tag[4]);
        void patchU32(size_t offset, uint32_t value);
};

#endif // STATEWRITER_H
