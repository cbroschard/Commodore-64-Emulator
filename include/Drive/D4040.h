// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D4040_H
#define D4040_H

#include <cstdint>
#include <vector>
#include "CPU.h"
#include "Drive/D4040Bus.h"
#include "Drive/D4040FDC.h"
#include "Drive/D4040Memory.h"
#include "Drive/Drive.h"
#include "Drive/IDriveIndicatorView.h"
#include "Drive/IDrivePositionView.h"
#include "Drive/IDriveUiView.h"
#include "GCR/GCRCodec.h"
#include "GCR/GCRTrackStream.h"
#include "IEEE488/IEEE488Device.h"
#include "IRQLine.h"
#include "StateReader.h"
#include "StateWriter.h"

class D4040 : public Drive, public IDriveIndicatorView, public IDrivePositionView, public IDriveUiView
{
    public:
        D4040();
        virtual ~D4040();

        void reset() override;

        // State Management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        // Drive execution
        void tick(uint32_t cycles) override;
        void updateIRQ() override;

    protected:
        bool motorOn;

    private:
        D4040Bus d4040Bus;
        D4040Memory d4040mem;
        CPU driveCPU;
        D4040FDC d4040fdc;
        GCRTrackStream gcrTrack;
        GCRCodec gcrCodec;
        IEEE488Device ieee488Device;
        IRQLine IRQ;
};

#endif // D4040_H
