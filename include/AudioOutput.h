// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef AUDIOOUTPUT_H
#define AUDIOOUTPUT_H

#include "SDL2/SDL.h"
#include "SID/sid.h"

class AudioOutput
{
    public:
        AudioOutput();
        virtual ~AudioOutput();

        inline void attachSIDInstance(SID* sid) { this->sid = sid; }

        bool playAudio();
        void pauseAudio();
        void stopAudio();
        void resumeAudio();
        inline int getBlockSamples() const { return obtainedSpec.samples; }
        inline int getSampleRate() const { return obtainedSpec.freq; }
        void fillAudioBuffer(Uint8* stream, int len);

    protected:

    private:
        // Non-owning pointers
        SID* sid;

        static const int SAMPLE_RATE = 44100;
        static const int CHANNELS = 2;
        static const int BUFFER_SIZE = 2048;
        static const int SCALE = 2;

        SDL_AudioSpec desired{};
        SDL_AudioSpec obtainedSpec{};
        SDL_AudioDeviceID dev;
};

#endif // AUDIOOUTPUT_H
