// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef RS232DEVICE_H
#define RS232DEVICE_H


class RS232Device
{
    public:
        RS232Device();
        virtual ~RS232Device();

        // Pointer attachment
        inline void attachPeerDevice(RS232Device* peer) { this->peer = peer; }

        // Setters
        void setDTR(bool state);
        void setRTS(bool state);
        inline void setCTS(bool state) { cts = state; }
        inline void setRXD(bool state) { rxd = state; }
        inline void setDSR(bool state) { dsr = state; }
        inline void setDCD(bool state) { dcd = state; }
        inline void setRI(bool state) { ri = state; }

        // Getters
        inline bool getRXD() const { return rxd; }
        inline bool getDSR() const { return dsr; }
        inline bool getCTS() const { return cts; }
        inline bool getRI() const { return ri; }
        inline bool getDCD() const { return dcd; }

    protected:

    private:

        // Non-owning Pointers
        RS232Device* peer = nullptr;

        bool dtr;
        bool dsr;
        bool rts;
        bool rxd;
        bool cts;
        bool dcd;
        bool ri;
};

#endif // RS232DEVICE_H
