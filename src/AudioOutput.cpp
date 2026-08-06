// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <vector>
#include "AudioOutput.h"

namespace
{
    void SDLCALL audioCallback(void* userdata, SDL_AudioStream* stream, int additionalAmount, int totalAmount)
    {
        (void)totalAmount;

        auto* audioOutput = static_cast<AudioOutput*>(userdata);

        if (!audioOutput || !stream || additionalAmount <= 0)
            return;

        const int bytesPerFrame =  static_cast<int>(sizeof(Sint16)) * 2;
        const int frameCount = additionalAmount / bytesPerFrame;

        if (frameCount <= 0)
            return;

        std::vector<Sint16> buffer(frameCount * 2);
        const int bytesToGenerate = frameCount * bytesPerFrame;
        audioOutput->fillAudioBuffer(reinterpret_cast<Uint8*>(buffer.data()), bytesToGenerate);

        if (!SDL_PutAudioStreamData(stream, buffer.data(), bytesToGenerate))
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Couldn't provide audio data: %s", SDL_GetError());
    }
}


AudioOutput::AudioOutput() :
    sid(nullptr),
    stream(nullptr)
{

}

AudioOutput::~AudioOutput()
{
    stopAudio();
}

void AudioOutput::fillAudioBuffer(Uint8* outputBuffer, int len)
{
    if (!outputBuffer || len <= 0)
        return;

    Sint16* buffer = reinterpret_cast<Sint16*>(outputBuffer);
    const int numSamplesPerChannel = len / (sizeof(Sint16) * CHANNELS);

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
    if (stream)
        return true;

    SDL_AudioSpec spec{};
    spec.freq = SAMPLE_RATE;
    spec.format = SDL_AUDIO_S16;
    spec.channels = CHANNELS;

    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audioCallback, this);

    if (!stream)
    {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Couldn't open audio: %s", SDL_GetError());
        return false;
    }

    return true;
}

void AudioOutput::pauseAudio()
{
    if (stream)
    {
        if (!SDL_PauseAudioStreamDevice(stream))
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Couldn't pause audio: %s", SDL_GetError());
    }
}

void AudioOutput::stopAudio()
{
    if (stream)
    {
        SDL_PauseAudioStreamDevice(stream);
        SDL_DestroyAudioStream(stream);
        stream = nullptr;
    }

    sid = nullptr;
}

void AudioOutput::resumeAudio()
{
    if (stream)
    {
        if (!SDL_ResumeAudioStreamDevice(stream))
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Couldn't resume audio: %s", SDL_GetError());
    }
}
