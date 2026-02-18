// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef T64_H
#define T64_H

#include "TapeImage.h"

class T64 : public TapeImage
{
    public:
        T64();
        virtual ~T64();

        inline void attachLoggingInstance(Logging* logger) { this->logger = logger; }

        bool loadTape(const std::string& filePath, VideoMode mode) override;
        void rewind() override;
        void simulateLoading() override;
        bool currentBit() const override;

        // Interface for cassette to load into memory
        bool hasLoadedFile() const;
        uint16_t getPrgStart() const;
        uint16_t getPrgEnd() const;
        const uint8_t* getPrgData() const;
        bool isT64() const override;

    protected:

        std::vector<uint8_t> tapeData; // Vector to store tape data

    private:

        // Non-owning pointers
        Logging* logger = nullptr;

        bool loadFile(const std::string& path, std::vector<uint8_t>& buffer) override;
        bool validateHeader() override;

        // Parsed PRG info
        bool fileLoaded;
        uint16_t prgStart;
        uint16_t prgEnd;
        uint32_t prgPtr;
        uint16_t prgLen;
        size_t curByte;

        #pragma pack(push,1)
        struct tapeHeader
        {
            char headerID[32];        // $00: "C64S tape image file"
            uint16_t version;         // $20: Version
            uint16_t maxEntries;      // $22: Directory entry slots
            uint16_t usedEntries;     // $24: Used entries
            char reserved2[2];        // $26: reserved
            char tapeName[24];        // $28: Tape name
            char reserved3[12];       // $42: reserved
        } header;
        #pragma pack(pop)
};

#endif // T64_H
