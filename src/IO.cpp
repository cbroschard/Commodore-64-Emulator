// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "IO.h"
#include "Vic.h"

#include <algorithm>
#include <stdexcept>

namespace
{
    void audioCallback(void* userdata, Uint8* stream, int len)
    {
        IO* io = static_cast<IO*>(userdata);
        io->fillAudioBuffer(stream, len);
    }
}

IO::IO() :
    visibleScreenWidth(320),
    visibleScreenHeight(200),
    borderSize(32),
    screenWidthWithBorder(320 + 2 * 32),
    screenHeightWithBorder(200 + 2 * 32),
    logger(nullptr),
    sidchip(nullptr),
    vicII(nullptr),
    window(nullptr),
    renderer(nullptr),
    screenTexture(nullptr),
    dev(0),
    readyBuffer(nullptr),
    setLogging(false)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
    {
        throw std::runtime_error(std::string("SDL Video/Sound Init Failed: ") + SDL_GetError());
    }

    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC) != 0)
    {
        throw std::runtime_error(std::string("SDL game controller subsystem failed to initialize: ") + SDL_GetError());
    }

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

    if (window == nullptr)
    {
        SDL_Quit();
        throw std::runtime_error(std::string("Unable to create SDL Window: ") + SDL_GetError());
    }

    SDL_SetWindowMinimumSize(window, screenWidthWithBorder, screenHeightWithBorder);

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == nullptr)
    {
        SDL_DestroyWindow(window);
        SDL_Quit();
        throw std::runtime_error(std::string("Unable to create renderer: ") + SDL_GetError());
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    ImGui::StyleColorsLight();

    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    frontBuffer.resize(screenWidthWithBorder * screenHeightWithBorder);
    backBuffer.resize(screenWidthWithBorder * screenHeightWithBorder);

    screenTexture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        screenWidthWithBorder,
        screenHeightWithBorder
    );

    if (screenTexture == nullptr)
    {
        throw std::runtime_error(std::string("Couldn't create texture: ") + SDL_GetError());
    }

#if SDL_VERSION_ATLEAST(2,0,12)
    SDL_SetTextureScaleMode(screenTexture, SDL_ScaleModeNearest);
#endif

    SDL_QueryTexture(screenTexture, &textureFormat, nullptr, nullptr, nullptr);
    SDL_PixelFormat* fmt = SDL_AllocFormat(textureFormat);

    for (int i = 0; i < 16; ++i)
    {
        SDL_Color c = getColor(static_cast<uint8_t>(i));
        palette32[i] = SDL_MapRGBA(fmt, c.r, c.g, c.b, 0xFF);
    }

    SDL_FreeFormat(fmt);
}

IO::~IO()
{
    stopAudio();

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    if (screenTexture) SDL_DestroyTexture(screenTexture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);

    SDL_Quit();
}

void IO::startRenderThread(std::atomic<bool>&)
{
    // no-op: rendering now happens on the main thread
}

void IO::stopRenderThread(std::atomic<bool>&)
{
    // no-op: rendering now happens on the main thread
}

void IO::fillAudioBuffer(Uint8* stream, int len)
{
    Sint16* buffer = reinterpret_cast<Sint16*>(stream);
    int numSamplesPerChannel = len / (sizeof(Sint16) * CHANNELS);

    for (int i = 0; i < numSamplesPerChannel; ++i)
    {
        double s = 0.0;
        if (sidchip)
            s = sidchip->popSample();

        if (s > 1.0) s = 1.0;
        else if (s < -1.0) s = -1.0;

        const auto sample16 = static_cast<Sint16>(s * 32767.0);

        buffer[i * CHANNELS + 0] = sample16;
        buffer[i * CHANNELS + 1] = sample16;
    }
}

bool IO::playAudio()
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

    SDL_PauseAudioDevice(dev, 0);
    return true;
}

void IO::stopAudio()
{
    if (dev != 0)
    {
        SDL_PauseAudioDevice(dev, 1);
        SDL_CloseAudioDevice(dev);
        dev = 0;
    }

    sidchip = nullptr;
}

void IO::renderBackgroundLine(int row, uint8_t color, int x0, int x1)
{
    const int W  = screenWidthWithBorder;
    const int y0 = borderSize;
    const int y1 = y0 + visibleScreenHeight;

    if (row < y0 || row >= y1) return;

    uint32_t pix = palette32[color & 0x0F];
    uint32_t* dst = backBuffer.data() + row * W;
    std::fill(dst + x0, dst + x1, pix);
}

void IO::renderBorderLine(int row, uint8_t color, int x0, int x1)
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

void IO::setPixel(int x, int y, uint8_t colorIndex)
{
    if (x < 0 || x >= screenWidthWithBorder || y < 0 || y >= screenHeightWithBorder)
        return;

    backBuffer[y * screenWidthWithBorder + x] = palette32[colorIndex & 0x0F];
}

void IO::setPixel(int x, int y, uint8_t colorIndex, int hardwareX)
{
    int shiftedX = x - hardwareX;

    if (shiftedX < 0 || shiftedX >= screenWidthWithBorder ||
        y < 0 || y >= screenHeightWithBorder)
    {
        return;
    }

    backBuffer[y * screenWidthWithBorder + shiftedX] = palette32[colorIndex & 0x0F];
}

SDL_Color IO::getColor(uint8_t colorCode)
{
    static const SDL_Color colors[16] =
    {
        {0, 0, 0},
        {255, 255, 255},
        {171, 49, 38},
        {102, 218, 255},
        {187, 63, 184},
        {85, 206, 88},
        {0, 0, 170},
        {234, 245, 124},
        {221, 136, 85},
        {102, 68, 0},
        {255, 119, 119},
        {51, 51, 51},
        {119, 119, 119},
        {170, 255, 102},
        {0, 136, 255},
        {187, 187, 187}
    };

    if (colorCode > 15)
        return colors[0];

    return colors[colorCode];
}

void IO::finishFrameAndSignal()
{
    std::lock_guard<std::mutex> lk(renderMut);
    backBuffer.swap(frontBuffer);
    readyBuffer.store(frontBuffer.data(), std::memory_order_release);
}

SDL_Rect IO::computeDestinationRect(int outputW, int outputH) const
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

void IO::handleEvent(const SDL_Event& e, std::atomic<bool>& runningFlag)
{
    sdlMon.handleEvent(e);

    const bool monitorOpen =
        monitorOpenCallback ? monitorOpenCallback() : sdlMon.isOpen();

    if (monitorOpen)
    {
        if (e.type == SDL_TEXTINPUT ||
            e.type == SDL_TEXTEDITING ||
            e.type == SDL_KEYDOWN ||
            e.type == SDL_KEYUP)
        {
            if (e.type == SDL_QUIT)
                runningFlag = false;
            return;
        }
    }

    ImGui_ImplSDL2_ProcessEvent(const_cast<SDL_Event*>(&e));

    ImGuiIO& io = ImGui::GetIO();
    if (inputCallback)
    {
        const bool kb_ok = !io.WantCaptureKeyboard || (e.type != SDL_KEYDOWN && e.type != SDL_KEYUP);
        const bool ms_ok = !io.WantCaptureMouse || (e.type < SDL_MOUSEMOTION || e.type > SDL_MOUSEWHEEL);
        if (kb_ok && ms_ok)
            inputCallback(e);
    }

    if (e.type == SDL_QUIT)
        runningFlag = false;
}

void IO::renderFrame(std::atomic<bool>& runningFlag)
{
    (void)runningFlag;

    uint32_t* lastBuf = readyBuffer.exchange(nullptr, std::memory_order_acquire);
    if (!lastBuf)
        lastBuf = frontBuffer.data();

    std::lock_guard<std::mutex> lk(renderMut);

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();

    const bool monitorOpen = monitorOpenCallback ? monitorOpenCallback() : sdlMon.isOpen();

    if (monitorOpen) SDL_StartTextInput();
    else SDL_StopTextInput();

    ImGui::NewFrame();
    if (guiCallback) guiCallback();
    ImGui::Render();

    int outputW = 0;
    int outputH = 0;
    SDL_GetRendererOutputSize(renderer, &outputW, &outputH);

    SDL_Rect dstRect = computeDestinationRect(outputW, outputH);
    const int pitch = screenWidthWithBorder * static_cast<int>(sizeof(uint32_t));

    SDL_UpdateTexture(screenTexture, nullptr, lastBuf, pitch);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, screenTexture, nullptr, &dstRect);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);

    if (sdlMon.isOpen())
        sdlMon.render();
}

void IO::setScreenDimensions(int visibleW, int visibleH, int border)
{
    std::lock_guard<std::mutex> lk(renderMut);

    visibleScreenWidth = visibleW;
    visibleScreenHeight = visibleH;
    borderSize = border;
    screenWidthWithBorder = visibleW + 2 * borderSize;
    screenHeightWithBorder = visibleH + 2 * borderSize;

    frontBuffer.assign(screenWidthWithBorder * screenHeightWithBorder, 0);
    backBuffer.assign(screenWidthWithBorder * screenHeightWithBorder, 0);

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
