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

        struct T64Entry
        {
            uint8_t entryType = 0;
            uint8_t fileType = 0;

            uint16_t startAddress = 0;
            uint16_t endAddress = 0;

            uint32_t dataOffset = 0;
            uint32_t dataLength = 0;

            std::string filename;
        };

        const std::vector<T64Entry>& getEntries() const;
        bool selectEntry(size_t index);
        size_t getSelectedEntry() const;

    protected:
        std::vector<uint8_t> tapeData; // Vector to store tape data

    private:
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

        std::vector<T64Entry> entries;
        size_t selectedEntry;

        // Parsed PRG info
        bool fileLoaded;
        uint16_t prgStart;
        uint16_t prgEnd;
        uint32_t prgPtr;
        uint32_t prgLen;

        void reset();

        bool loadFile(const std::string& path, std::vector<uint8_t>& buffer) override;
        bool validateHeader() override;
};

#endif // T64_H
