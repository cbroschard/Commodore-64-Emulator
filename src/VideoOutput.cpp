// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <algorithm>
#include <stdexcept>
#include <string>
#include "VideoOutput.h"

VideoOutput::VideoOutput() :
    window(nullptr),
    renderer(nullptr),
    screenTexture(nullptr),
    visibleScreenWidth(320),
    visibleScreenHeight(200),
    borderSize(32),
    screenWidthWithBorder(320 + 2 * 32),
    screenHeightWithBorder(200 + 2 * 32),
    readyBuffer(nullptr)
{
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

    const Uint32 windowFlags =
        SDL_WINDOW_SHOWN |
        SDL_WINDOW_RESIZABLE |
        SDL_WINDOW_ALLOW_HIGHDPI;

    window = SDL_CreateWindow(
        "Commodore 64 Emulator",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        screenWidthWithBorder * SCALE,
        screenHeightWithBorder * SCALE,
        windowFlags
    );

    if (!window)
    {
        throw std::runtime_error(
            std::string("Unable to create SDL window: ") +
            SDL_GetError()
        );
    }

    SDL_SetWindowMinimumSize(
        window,
        screenWidthWithBorder,
        screenHeightWithBorder
    );

    renderer = SDL_CreateRenderer(
        window,
        -1,
        SDL_RENDERER_ACCELERATED
    );

    if (!renderer)
    {
        SDL_DestroyWindow(window);
        window = nullptr;

        throw std::runtime_error(
            std::string("Unable to create SDL renderer: ") +
            SDL_GetError()
        );
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsLight();

    if (!ImGui_ImplSDL2_InitForSDLRenderer(window, renderer))
    {
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);

        renderer = nullptr;
        window = nullptr;

        throw std::runtime_error(
            "Unable to initialize ImGui SDL2 backend"
        );
    }

     if (!ImGui_ImplSDLRenderer2_Init(renderer))
    {
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();

        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);

        renderer = nullptr;
        window = nullptr;

        throw std::runtime_error(
            "Unable to initialize ImGui renderer backend"
        );
    }

    frontBuffer.assign(screenWidthWithBorder * screenHeightWithBorder, 0);

    backBuffer.assign(screenWidthWithBorder * screenHeightWithBorder, 0);

    screenTexture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        screenWidthWithBorder,
        screenHeightWithBorder
    );

    if (!screenTexture)
    {
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();

        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);

        renderer = nullptr;
        window = nullptr;

        throw std::runtime_error(
            std::string("Unable to create screen texture: ") +
            SDL_GetError()
        );
    }

    #if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetTextureScaleMode(
        screenTexture,
        SDL_ScaleModeNearest
    );
#endif

    SDL_PixelFormat* format =
        SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888);

    if (!format)
    {
        throw std::runtime_error(
            std::string("Unable to allocate pixel format: ") +
            SDL_GetError()
        );
    }

    for (int i = 0; i < 16; ++i)
    {
        const SDL_Color color =
            getColor(static_cast<uint8_t>(i));

        palette32[i] = SDL_MapRGBA(
            format,
            color.r,
            color.g,
            color.b,
            0xFF
        );
    }

    SDL_FreeFormat(format);
}

VideoOutput::~VideoOutput()
{
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    if (screenTexture)
    {
        SDL_DestroyTexture(screenTexture);
        screenTexture = nullptr;
    }

    if (renderer)
    {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }

    if (window)
    {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
}

void VideoOutput::renderBackgroundLine(int row, uint8_t color, int x0, int x1)
{
    const int width = screenWidthWithBorder;
    const int firstVisibleRow = borderSize;
    const int lastVisibleRow = firstVisibleRow + visibleScreenHeight;

    if (row < firstVisibleRow || row >= lastVisibleRow)
        return;

    x0 = std::clamp(x0, 0, width);
    x1 = std::clamp(x1, 0, width);

    if (x0 >= x1)
        return;

    const uint32_t pixel = palette32[color & 0x0F];

    uint32_t* destination = backBuffer.data() + row * width;

    std::fill(destination + x0, destination + x1, pixel
    );
}

void VideoOutput::renderBorderLine(int row, uint8_t color, int x0, int x1)
{
    const int W = screenWidthWithBorder;
    if (row < 0 || row >= screenHeightWithBorder) return;

    const int y0 = borderSize;
    const int y1 = y0 + visibleScreenHeight;

    uint32_t* dst = backBuffer.data() + row * W;
    uint32_t pix = palette32[color & 0x0F];

    if (row < y0 || row >= y1)
    {
        std::fill(dst, dst + W, pix);
    }
    else
    {
        std::fill(dst, dst + x0, pix);
        std::fill(dst + x1, dst + W, pix);
    }
}

void VideoOutput::setPixel(int x, int y, uint8_t colorIndex)
{
    if (x < 0 || x >= screenWidthWithBorder || y < 0 || y >= screenHeightWithBorder)
        return;

    backBuffer[y * screenWidthWithBorder + x] = palette32[colorIndex & 0x0F];
}

void VideoOutput::setPixel(int x, int y, uint8_t colorIndex, int hardwareX)
{
    int shiftedX = x - hardwareX;

    if (shiftedX < 0 || shiftedX >= screenWidthWithBorder ||
        y < 0 || y >= screenHeightWithBorder)
    {
        return;
    }

    backBuffer[y * screenWidthWithBorder + shiftedX] = palette32[colorIndex & 0x0F];
}

void VideoOutput::finishFrameAndSignal()
{
    std::lock_guard<std::mutex> lk(renderMut);
    backBuffer.swap(frontBuffer);
    readyBuffer.store(frontBuffer.data(), std::memory_order_release);
}

void VideoOutput::renderFrame(std::atomic<bool>& runningFlag)
{
    (void)runningFlag;

    if (!renderer || !screenTexture)
        return;

    uint32_t* lastBuf =
        readyBuffer.exchange(
            nullptr,
            std::memory_order_acquire
        );

    if (!lastBuf)
        lastBuf = frontBuffer.data();

    std::lock_guard<std::mutex> lock(renderMut);

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();

    const bool monitorOpen =
        monitorOpenCallback
            ? monitorOpenCallback()
            : sdlMon.isOpen();

    if (monitorOpen)
        SDL_StartTextInput();
    else
        SDL_StopTextInput();

    ImGui::NewFrame();

    if (guiCallback)
        guiCallback();

    ImGui::Render();

    int outputW = 0;
    int outputH = 0;

    if (SDL_GetRendererOutputSize(
            renderer,
            &outputW,
            &outputH) != 0)
    {
        return;
    }

    const SDL_Rect destination =
        computeDestinationRect(outputW, outputH);

    const int pitch =
        screenWidthWithBorder *
        static_cast<int>(sizeof(uint32_t));

    if (SDL_UpdateTexture(
            screenTexture,
            nullptr,
            lastBuf,
            pitch) != 0)
    {
        SDL_Log(
            "SDL_UpdateTexture failed: %s",
            SDL_GetError()
        );
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    SDL_RenderCopy(
        renderer,
        screenTexture,
        nullptr,
        &destination
    );

    ImGui_ImplSDLRenderer2_RenderDrawData(
        ImGui::GetDrawData(),
        renderer
    );

    SDL_RenderPresent(renderer);

    if (sdlMon.isOpen())
        sdlMon.render();
}

void VideoOutput::handleEvent(const SDL_Event& event, std::atomic<bool>& running)
{
    sdlMon.handleEvent(event);

    const bool monitorOpen =
        monitorOpenCallback
            ? monitorOpenCallback()
            : sdlMon.isOpen();

    SDL_Event mutableEvent = event;
    ImGui_ImplSDL2_ProcessEvent(&mutableEvent);

    if (event.type == SDL_QUIT)
    {
        running = false;
        return;
    }

    if (monitorOpen)
    {
        if (event.type == SDL_TEXTINPUT ||
            event.type == SDL_TEXTEDITING ||
            event.type == SDL_KEYDOWN ||
            event.type == SDL_KEYUP)
        {
            return;
        }
    }

    if (!inputCallback)
        return;

    const ImGuiIO& imguiIO = ImGui::GetIO();

    const bool keyboardEvent =
        event.type == SDL_KEYDOWN ||
        event.type == SDL_KEYUP ||
        event.type == SDL_TEXTINPUT ||
        event.type == SDL_TEXTEDITING;

    const bool mouseEvent =
        event.type == SDL_MOUSEMOTION ||
        event.type == SDL_MOUSEBUTTONDOWN ||
        event.type == SDL_MOUSEBUTTONUP ||
        event.type == SDL_MOUSEWHEEL;

    if (keyboardEvent && imguiIO.WantCaptureKeyboard)
        return;

    if (mouseEvent && imguiIO.WantCaptureMouse)
        return;

    inputCallback(event);
}

void VideoOutput::setScreenDimensions(int visibleW, int visibleH, int border)
{
    std::lock_guard<std::mutex> lk(renderMut);

    visibleScreenWidth = visibleW;
    visibleScreenHeight = visibleH;
    borderSize = border;
    screenWidthWithBorder = visibleW + 2 * borderSize;
    screenHeightWithBorder = visibleH + 2 * borderSize;

    frontBuffer.assign(screenWidthWithBorder * screenHeightWithBorder, 0);
    backBuffer.assign(screenWidthWithBorder * screenHeightWithBorder, 0);
    readyBuffer.store(nullptr, std::memory_order_release);

    if (screenTexture)
    {
        SDL_DestroyTexture(screenTexture);
        screenTexture = nullptr;
    }

    screenTexture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        screenWidthWithBorder,
        screenHeightWithBorder
    );

    if (!screenTexture)
    {
        throw std::runtime_error(std::string("Couldn't recreate texture: ") + SDL_GetError());
    }

#if SDL_VERSION_ATLEAST(2,0,12)
    SDL_SetTextureScaleMode(screenTexture, SDL_ScaleModeNearest);
#endif

    SDL_SetWindowMinimumSize(window, screenWidthWithBorder, screenHeightWithBorder);
}

SDL_Color VideoOutput::getColor(uint8_t colorCode)
{
    static const SDL_Color colors[16] =
    {
        {0x00, 0x00, 0x00}, // black
        {0xFF, 0xFF, 0xFF}, // white
        {0x88, 0x40, 0x00}, // red
        {0x68, 0xD0, 0xA8}, // cyan
        {0xA8, 0x38, 0xA0}, // purple
        {0x50, 0xB8, 0x18}, // green
        {0x18, 0x10, 0x90}, // blue
        {0xF0, 0xE8, 0x58}, // yellow
        {0xA0, 0x48, 0x00}, // orange
        {0x47, 0x2B, 0x1B}, // brown
        {0xC8, 0x78, 0x70}, // light red
        {0x48, 0x48, 0x48}, // dark gray
        {0x80, 0x80, 0x80}, // gray
        {0x98, 0xFF, 0x98}, // light green
        {0x50, 0x90, 0xD0}, // light blue
        {0xB8, 0xB8, 0xB8}  // light gray
    };

    return colors[colorCode & 0x0F];
}

SDL_Rect VideoOutput::computeDestinationRect(int outputW, int outputH) const
{
    const float srcW = static_cast<float>(screenWidthWithBorder);
    const float srcH = static_cast<float>(screenHeightWithBorder);

    const float scaleX = static_cast<float>(outputW) / srcW;
    const float scaleY = static_cast<float>(outputH) / srcH;
    const float scale  = std::min(scaleX, scaleY);

    const int drawW = std::max(1, static_cast<int>(srcW * scale));
    const int drawH = std::max(1, static_cast<int>(srcH * scale));

    SDL_Rect dst;
    dst.w = drawW;
    dst.h = drawH;
    dst.x = (outputW - drawW) / 2;
    dst.y = (outputH - drawH) / 2;
    return dst;
}
