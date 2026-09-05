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

namespace
{
    // Pepto NTSC C64 palette
    // Based on Philip "Pepto" Timmermann's VIC-II color analysis:
    // https://www.pepto.de/projects/colorvic/
    // RGB values derived from VIC-II color characteristics.
   const std::array<SDL_Color, 16> NTSC_COLORS =
    {{
        {0x00, 0x00, 0x00, 0xFF}, // $0 Black
        {0xFF, 0xFF, 0xFF, 0xFF}, // $1 White

        {0x7C, 0x35, 0x2B, 0xFF}, // $2 Red
        {0x5A, 0xA6, 0xB1, 0xFF}, // $3 Cyan

        {0x69, 0x41, 0x7B, 0xFF}, // $4 Purple
        {0x5D, 0x86, 0x43, 0xFF}, // $5 Green

        {0x21, 0x2E, 0x78, 0xFF}, // $6 Blue
        {0xCF, 0xBE, 0x6F, 0xFF}, // $7 Yellow

        {0x89, 0x4A, 0x26, 0xFF}, // $8 Orange
        {0x5B, 0x33, 0x00, 0xFF}, // $9 Brown

        {0xAF, 0x64, 0x59, 0xFF}, // $A Light Red
        {0x43, 0x43, 0x43, 0xFF}, // $B Dark Gray

        {0x6B, 0x6B, 0x6B, 0xFF}, // $C Gray
        {0xA0, 0xCB, 0x84, 0xFF}, // $D Light Green

        {0x56, 0x65, 0xB3, 0xFF}, // $E Light Blue
        {0x95, 0x95, 0x95, 0xFF}  // $F Light Gray
    }};

    const std::array<SDL_Color, 16> PAL_COLORS =
    {{
        {0x00, 0x00, 0x00, 0xFF},
        {0xFF, 0xFF, 0xFF, 0xFF},
        {0x68, 0x37, 0x2B, 0xFF},
        {0x70, 0xA4, 0xB2, 0xFF},
        {0x6F, 0x3D, 0x86, 0xFF},
        {0x58, 0x8D, 0x43, 0xFF},
        {0x35, 0x28, 0x79, 0xFF},
        {0xB8, 0xC7, 0x6F, 0xFF},
        {0x6F, 0x4F, 0x25, 0xFF},
        {0x43, 0x39, 0x00, 0xFF},
        {0x9A, 0x67, 0x59, 0xFF},
        {0x44, 0x44, 0x44, 0xFF},
        {0x6C, 0x6C, 0x6C, 0xFF},
        {0x9A, 0xD2, 0x84, 0xFF},
        {0x6C, 0x5E, 0xB5, 0xFF},
        {0x95, 0x95, 0x95, 0xFF}
    }};
}

VideoOutput::VideoOutput() :
    window(nullptr),
    renderer(nullptr),
    screenTexture(nullptr),
    mode_(VideoMode::NTSC),
    visibleScreenWidth(320),
    visibleScreenHeight(200),
    borderSize(32),
    screenWidthWithBorder(320 + 2 * 32),
    screenHeightWithBorder(200 + 2 * 32),
    frameReady(false)
{
    const SDL_WindowFlags windowFlags = SDL_WINDOW_RESIZABLE;

    window = SDL_CreateWindow("Commodore 64 Emulator", screenWidthWithBorder * SCALE, screenHeightWithBorder * SCALE, windowFlags);

    if (!window)
        throw std::runtime_error(std::string("Unable to create SDL window: ") + SDL_GetError());

    SDL_SetWindowMinimumSize(window, screenWidthWithBorder, screenHeightWithBorder);

    renderer = SDL_CreateRenderer(window, nullptr);

    if (!renderer)
    {
        SDL_DestroyWindow(window);
        window = nullptr;

        throw std::runtime_error(std::string("Unable to create SDL renderer: ") + SDL_GetError());
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiStyle& style = ImGui::GetStyle();

    ImGui::StyleColorsLight();

    const float dpiScale = SDL_GetWindowDisplayScale(window);

    style.FontSizeBase = 16.0f;
    style.FontScaleDpi = dpiScale;
    style.ScaleAllSizes(dpiScale);

    if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer))
    {
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);

        renderer = nullptr;
        window = nullptr;

        throw std::runtime_error("Unable to initialize ImGui SDL3 backend");
    }

     if (!ImGui_ImplSDLRenderer3_Init(renderer))
    {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();

        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);

        renderer = nullptr;
        window = nullptr;

        throw std::runtime_error(
            "Unable to initialize ImGui renderer backend"
        );
    }

    const size_t bufferSize = static_cast<size_t>(screenWidthWithBorder) * static_cast<size_t>(screenHeightWithBorder);

    frontBuffer.assign(bufferSize, 0);
    backBuffer.assign(bufferSize, 0);

    screenTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, screenWidthWithBorder,
        screenHeightWithBorder);

    if (!screenTexture)
    {
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();

        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);

        renderer = nullptr;
        window = nullptr;

        throw std::runtime_error(std::string("Unable to create screen texture: ") + SDL_GetError());
    }

    if (!SDL_SetTextureScaleMode(screenTexture, SDL_SCALEMODE_NEAREST))
        SDL_Log("Unable to set nearest texture filtering: %s", SDL_GetError());

    rebuildPalette();
}

VideoOutput::~VideoOutput()
{
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
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

void VideoOutput::setMode(VideoMode mode)
{
    mode_ = mode;

    rebuildPalette();
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

    if (row < 0 || row >= screenHeightWithBorder)
        return;

    const int y0 = borderSize;
    const int y1 = y0 + visibleScreenHeight;

    uint32_t* dst = backBuffer.data() + row * W;
    const uint32_t pix = palette32[color & 0x0F];

    if (row < y0 || row >= y1)
    {
        std::fill(dst, dst + W, pix);
        return;
    }

    x0 = std::clamp(x0, 0, W);
    x1 = std::clamp(x1, 0, W);

    if (x0 > x1)
        std::swap(x0, x1);

    std::fill(dst, dst + x0, pix);
    std::fill(dst + x1, dst + W, pix);
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
    std::lock_guard<std::mutex> lock(renderMut);

    backBuffer.swap(frontBuffer);
    frameReady = true;
}

void VideoOutput::renderFrame(std::atomic<bool>& runningFlag)
{
    (void)runningFlag;

    std::lock_guard<std::mutex> lock(renderMut);

    if (!renderer || !screenTexture)
        return;

    if (frontBuffer.empty())
        return;

    frameReady = false;

    const uint32_t* lastBuf = frontBuffer.data();

    if (!lastBuf)
        lastBuf = frontBuffer.data();

    if (!lastBuf || frontBuffer.empty())
        return;

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    if (guiCallback)
        guiCallback();

    ImGui::Render();

    int outputW = 0;
    int outputH = 0;

    if (!SDL_GetCurrentRenderOutputSize(renderer, &outputW, &outputH))
    {
        SDL_Log("Unable to get renderer output size: %s", SDL_GetError());
        return;
    }

    const SDL_FRect destination = computeDestinationRect(outputW, outputH);

    const int pitch = screenWidthWithBorder * static_cast<int>(sizeof(uint32_t));

    if (!SDL_UpdateTexture(screenTexture, nullptr, lastBuf, pitch))
        SDL_Log("SDL_UpdateTexture failed: %s", SDL_GetError());

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    if (!SDL_RenderTexture(renderer, screenTexture, nullptr, &destination))
        SDL_Log("SDL_RenderTexture failed: %s", SDL_GetError());

    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

    SDL_RenderPresent(renderer);

    if (sdlMon.isOpen())
        sdlMon.render();
}

void VideoOutput::handleEvent(const SDL_Event& event, std::atomic<bool>& running)
{
    sdlMon.handleEvent(event);

    const bool monitorOpen = monitorOpenCallback ? monitorOpenCallback() : sdlMon.isOpen();

    ImGui_ImplSDL3_ProcessEvent(&event);

    if (event.type == SDL_EVENT_QUIT)
    {
        running = false;
        return;
    }

    if (monitorOpen)
    {
        if (event.type == SDL_EVENT_TEXT_INPUT ||
            event.type == SDL_EVENT_TEXT_EDITING ||
            event.type == SDL_EVENT_KEY_DOWN ||
            event.type == SDL_EVENT_KEY_UP)
        {
            return;
        }
    }

    if (!inputCallback)
        return;

    const ImGuiIO& imguiIO = ImGui::GetIO();
    const bool keyboardEvent = event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP || event.type == SDL_EVENT_TEXT_INPUT ||
        event.type == SDL_EVENT_TEXT_EDITING;

    const bool mouseEvent = event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_WHEEL;

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

    const size_t bufferSize = static_cast<size_t>(screenWidthWithBorder) * static_cast<size_t>(screenHeightWithBorder);

    frontBuffer.assign(bufferSize, 0);
    backBuffer.assign(bufferSize, 0);
    frameReady = false;

    if (screenTexture)
    {
        SDL_DestroyTexture(screenTexture);
        screenTexture = nullptr;
    }

    screenTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, screenWidthWithBorder,
                                      screenHeightWithBorder);

    if (!screenTexture)
        throw std::runtime_error(std::string("Couldn't recreate texture: ") + SDL_GetError());

    if (!SDL_SetTextureScaleMode(screenTexture, SDL_SCALEMODE_NEAREST))
        SDL_Log("Unable to set nearest texture filtering: %s", SDL_GetError());

    SDL_SetWindowMinimumSize(window, screenWidthWithBorder, screenHeightWithBorder);
}

SDL_FRect VideoOutput::computeDestinationRect(int outputW, int outputH) const
{
    const float sourceWidth     = static_cast<float>(screenWidthWithBorder);
    const float sourceHeight    = static_cast<float>(screenHeightWithBorder);
    const float scaleX          = static_cast<float>(outputW) / sourceWidth;
    const float scaleY          = static_cast<float>(outputH) / sourceHeight;
    const float scale           = std::min(scaleX, scaleY);
    const float drawWidth       = std::max(1.0f, sourceWidth * scale);
    const float drawHeight      = std::max(1.0f, sourceHeight * scale);

    SDL_FRect destination{};

    destination.w = drawWidth;
    destination.h = drawHeight;

    destination.x = (static_cast<float>(outputW) - drawWidth) / 2.0f;
    destination.y = (static_cast<float>(outputH) - drawHeight) / 2.0f;

    return destination;
}

void VideoOutput::rebuildPalette()
{
    const auto& source = (mode_ == VideoMode::PAL) ? PAL_COLORS : NTSC_COLORS;

    const SDL_PixelFormatDetails* format = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA8888);

    if (!format)
        throw std::runtime_error(std::string("Unable to get pixel format details: ") + SDL_GetError());

    for (int i = 0; i < 16; ++i)
    {
        const SDL_Color& color = source[i];
        palette32[i] = SDL_MapRGBA(format, nullptr, color.r, color.g, color.b, 0xFF);
    }
}
