// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef AUDIOOUTPUT_H
#define AUDIOOUTPUT_H

#include <SDL3/SDL.h>
#include "SID/sid.h"

class AudioOutput
{
    public:
        AudioOutput();
        virtual ~AudioOutput();

        AudioOutput(const AudioOutput&) = delete;
        AudioOutput& operator=(const AudioOutput&) = delete;

        AudioOutput(AudioOutput&&) = delete;
        AudioOutput& operator=(AudioOutput&&) = delete;

        inline void attachSIDInstance(SID* sid) { this->sid = sid; }

        bool playAudio();
        void pauseAudio();
        void stopAudio();
        void resumeAudio();
        inline int getBlockSamples() const { return BUFFER_SIZE; }
        inline int getSampleRate() const { return SAMPLE_RATE; }
        void fillAudioBuffer(Uint8* buffer, int len);

        inline bool isPaused() const { return stream && SDL_AudioStreamDevicePaused(stream); }

    protected:

    private:
        // Non-owning pointer
        SID* sid;

        // SDL-owned audio stream handle
        SDL_AudioStream* stream;

        static constexpr int SAMPLE_RATE = 44100;
        static constexpr int CHANNELS = 2;
        static constexpr int BUFFER_SIZE = 2048;
};

#endif // AUDIOOUTPUT_H
