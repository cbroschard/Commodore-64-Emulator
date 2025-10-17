// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef IO_H
#define IO_H

#define SDL_MAIN_HANDLED

// Forward declaration
class Vic;

#include <condition_variable>
#include <cstdint>
#include <string>
#include <vector>
#include "sdl2/sdl.h"
#include "Logging.h"
#include "SID/SID.h"

class IO
{
    public:
        IO();
        virtual ~IO();

        // Screen constants
        int visibleScreenWidth;
        int visibleScreenHeight;
        int borderSize;
        int screenWidthWithBorder;
        int screenHeightWithBorder;

        inline void attachVICInstance(Vic* vicII) { this->vicII = vicII; }
        inline void attachSIDInstance(SID* sidchip) { this->sidchip = sidchip; }
        inline void attachLogInstance(Logging* logger) { this->logger = logger; }

        // Functions to update the border and background colors called by the VIC class
        void renderBackgroundLine(int row, uint8_t color, int x0, int x1);
        void renderBorderLine(int row, uint8_t color, int x0, int x1);

        // VIC Sprite Pixel Rendering
        void setPixel(int x, int y, uint8_t color);
        void setPixel(int x, int y, uint8_t color, int hardwareX);

        // Audio functions
        bool playAudio();
        inline int getBlockSamples() const { return obtainedSpec.samples; }
        inline int getSampleRate() const { return obtainedSpec.freq; }
        void fillAudioBuffer(Uint8* stream, int len);

        // called once from Computer::boot() to launch the render thread
        void startRenderThread(std::atomic<bool>& runningFlag);

        // called at shutdown
        void stopRenderThread(std::atomic<bool>& runningFlag);

        // Swap Buffers back to front and notify thread
        void swapBuffer();

        // Setter for screen geometry
        void setScreenDimensions(int visibleW, int visibleH, int border);

        void finishFrameAndSignal();

        // ML Monitor logging
        inline void setLog(bool enable) { setLogging = enable; }

    protected:

    private:

        // Non-owning pointers
        Logging* logger;
        SID* sidchip;
        Vic* vicII;

        // Audio constants
        static const int SAMPLE_RATE = 44100;
        static const int CHANNELS = 2;
        static const int BUFFER_SIZE = 737;

        // SDL setup
        static const int SCALE = 2; // Scale window size by a factor of 2
        SDL_Window *window;
        SDL_Renderer *renderer;
        SDL_Texture *screenTexture;

        // Audio processing
        SDL_AudioSpec desired{};
        SDL_AudioSpec obtainedSpec{};
        SDL_AudioDeviceID dev;

        std::vector<uint32_t> frontBuffer;
        std::vector<uint32_t> backBuffer;
        std::atomic<uint32_t*> readyBuffer;
        uint32_t textureFormat = 0;
        uint32_t palette32[16];

        std::mutex qMut;
        std::condition_variable qCond;
        std::thread rThread;

        bool setLogging;

        // Color helpers
        SDL_Color getColor(uint8_t colorCode);

        // Render thread
        void renderLoop(std::atomic<bool>& runningFlag);
};
#endif // IO_H
