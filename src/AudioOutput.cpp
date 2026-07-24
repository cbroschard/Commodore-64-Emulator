// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "AudioOutput.h"

namespace
{
    void audioCallback(void* userdata, Uint8* stream, int len)
    {
        AudioOutput* audioOutput = static_cast<AudioOutput*>(userdata);
        audioOutput->fillAudioBuffer(stream, len);
    }
}


AudioOutput::AudioOutput() :
    sid(nullptr),
    dev(0)
{

}

AudioOutput::~AudioOutput()
{
    stopAudio();
}

void AudioOutput::fillAudioBuffer(Uint8* stream, int len)
{
    Sint16* buffer = reinterpret_cast<Sint16*>(stream);
    int numSamplesPerChannel = len / (sizeof(Sint16) * CHANNELS);

    for (int i = 0; i < numSamplesPerChannel; ++i)
    {
        double s = 0.0;
        if (sid)
            s = sid->popSample();

        if (s > 1.0) s = 1.0;
        else if (s < -1.0) s = -1.0;

        const auto sample16 = static_cast<Sint16>(s * 32767.0);

        buffer[i * CHANNELS + 0] = sample16;
        buffer[i * CHANNELS + 1] = sample16;
    }
}

bool AudioOutput::playAudio()
{
    desired.freq = SAMPLE_RATE;
    desired.format = AUDIO_S16SYS;
    desired.channels = CHANNELS;
    desired.samples = BUFFER_SIZE;
    desired.callback = audioCallback;
    desired.userdata = this;

    dev = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtainedSpec, 0);

    if (!dev)
    {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Couldn't open audio: %s", SDL_GetError());
        return false;
    }

    // Start paused. EmulationSession will resume after SID has buffered samples.
    SDL_PauseAudioDevice(dev, 1);
    return true;
}

void AudioOutput::pauseAudio()
{
    if (dev != 0)
        SDL_PauseAudioDevice(dev, 1);
}

void AudioOutput::stopAudio()
{
    if (dev != 0)
    {
        SDL_PauseAudioDevice(dev, 1);
        SDL_CloseAudioDevice(dev);
        dev = 0;
    }

    sid = nullptr;
}

void AudioOutput::resumeAudio()
{
    if (dev != 0)
        SDL_PauseAudioDevice(dev, 0);
}
