// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef VIDEOOUTPUT_H
#define VIDEOOUTPUT_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <SDL3/SDL.h>
#include <utility>
#include <vector>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl3.h"
#include "imgui/imgui_impl_sdlrenderer3.h"
#include "IVideoSink.h"
#include "SDLMonitorWindow.h"

class VideoOutput final : public IVideoSink
{
    public:
        VideoOutput();
        ~VideoOutput() override;

        VideoOutput(const VideoOutput&) = delete;
        VideoOutput& operator=(const VideoOutput&) = delete;

        VideoOutput(VideoOutput&&) = delete;
        VideoOutput& operator=(VideoOutput&&) = delete;

        void renderBackgroundLine(int row, uint8_t color, int x0, int x1) override;
        void renderBorderLine(int row, uint8_t color, int x0, int x1) override;

        void setPixel(int x, int y, uint8_t color) override;
        void setPixel(int x, int y, uint8_t color, int hardwareX) override;

        void finishFrameAndSignal();
        void renderFrame(std::atomic<bool>& running);

        void handleEvent(const SDL_Event& e, std::atomic<bool>& runningFlag);

        void setScreenDimensions(int visibleW, int visibleH, int border) override;

        // imgui / UI wiring
        inline void setGuiCallback(std::function<void()> fn) { guiCallback = std::move(fn); }
        inline void setInputCallback(std::function<void(const SDL_Event&)> cb) { inputCallback = std::move(cb); }

        inline void setMonitorOpenCallback(std::function<bool()> fn) { monitorOpenCallback = std::move(fn); }

    protected:

    private:
        static constexpr int SCALE = 3;

        SDL_Window* window;
        SDL_Renderer* renderer;
        SDL_Texture* screenTexture;

        SDLMonitorWindow sdlMon;

        std::function<void()> guiCallback;
        std::function<void(const SDL_Event&)> inputCallback;
        std::function<bool()> monitorOpenCallback;

        // Screen constants
        int visibleScreenWidth;
        int visibleScreenHeight;
        int borderSize;
        int screenWidthWithBorder;
        int screenHeightWithBorder;

        std::vector<uint32_t> frontBuffer;
        std::vector<uint32_t> backBuffer;

        bool frameReady;

        uint32_t palette32[16];

        std::mutex renderMut;

        // Color helpers
        SDL_Color getColor(uint8_t colorCode);
        SDL_FRect computeDestinationRect(int outputW, int outputH) const;
};

#endif // VIDEOOUTPUT_H
