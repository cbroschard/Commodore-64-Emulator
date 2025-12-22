// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "IO.h"
#include "Vic.h"

IO::IO() :
    visibleScreenWidth(320),
    visibleScreenHeight(200),
    borderSize(32),
    screenWidthWithBorder(320 + 2 * 32),
    screenHeightWithBorder(200 + 2 * 32),
    logger(nullptr),
    sidchip(nullptr),
    vicII(nullptr),
    dev(0),
    readyBuffer(nullptr),
    setLogging(false)
{
    //Video and sound initialization
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
    {
        throw std::runtime_error(std::string("SDL Video/Sound Init Failed: ") + SDL_GetError());
    }

    window = SDL_CreateWindow("Commodore 64 Emulator",SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, screenWidthWithBorder * SCALE,
            screenHeightWithBorder * SCALE, SDL_WINDOW_SHOWN);
    if (window == nullptr)
    {
        SDL_Quit();
        throw std::runtime_error(std::string("Unable to create SDL Window: ") + SDL_GetError());
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == nullptr)
    {
        SDL_DestroyWindow(window);
        SDL_Quit();
        throw std::runtime_error(std::string("Unable to create renderer: ") + SDL_GetError());
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsLight();

    // Platform + renderer backends for SDL2 + SDL_Renderer
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    frontBuffer.resize(screenWidthWithBorder * screenHeightWithBorder);
    backBuffer.resize(screenWidthWithBorder * screenHeightWithBorder);

    screenTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, screenWidthWithBorder, screenHeightWithBorder);
    if (screenTexture == nullptr)
    {
        throw std::runtime_error(std::string("Couldn't create texture: ") + SDL_GetError());
    }

    SDL_QueryTexture(screenTexture, &textureFormat, nullptr, nullptr, nullptr);
    SDL_PixelFormat* fmt = SDL_AllocFormat(textureFormat);

    for (int i = 0; i < 16; ++i)
    {
        SDL_Color c = getColor(i);
        palette32[i] = SDL_MapRGBA(fmt, c.r, c.g, c.b, 0xFF);
    }
    SDL_FreeFormat(fmt);
}

IO::~IO()
{
    stopAudio();

    // ImGui shutdown
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    // SDL rendering shutdown
    if (screenTexture) SDL_DestroyTexture(screenTexture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
}

void IO::fillAudioBuffer(Uint8* stream, int len)
{
    Sint16* buffer = reinterpret_cast<Sint16*>(stream);
    int numSamplesPerChannel = len / (sizeof(Sint16) * CHANNELS);

    for (int i = 0; i < numSamplesPerChannel; ++i)
    {
        double s = 0.0;
        if (sidchip)
        {
            s = sidchip->popSample();
        }

        // Clamp and convert
        if (s > 1.0)
        {
            s = 1.0;
        }
        else if (s < -1.0)
        {
            s = -1.0;
        }
        auto sample16 = static_cast<Sint16>(s * 32767.0);

        // Write to stereo channels.
        buffer[i * CHANNELS + 0] = sample16; // Left channel
        buffer[i * CHANNELS + 1] = sample16; // Right channel
    }
}

void IO::startRenderThread(std::atomic<bool>& running)
{
    rThread = std::thread(&IO::renderLoop, this, std::ref(running));
}

void IO::stopRenderThread(std::atomic<bool>& runningFlag)
{
    {
        std::lock_guard lk(qMut);
        runningFlag = false; // tell the loop to bail
        qCond.notify_one(); // wake it if it’s sleeping
    }
    if (rThread.joinable())
    {
        rThread.join();
    }
}

void audioCallback(void* userdata, Uint8* stream, int len)
{
    IO* io = static_cast<IO*>(userdata);
    io->fillAudioBuffer(stream, len);
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

    SDL_PauseAudioDevice(dev, 0); // Start playback. Pass 0 to unpause.
    return true;
}

void IO::stopAudio()
{
        // Stop playback
        if (dev != 0)
        {
            SDL_PauseAudioDevice(dev, 1);

            // Close device: SDL guarantees no more callbacks after this returns
            SDL_CloseAudioDevice(dev);
            dev = 0;
        }
        sidchip = nullptr;
}

void IO::renderBackgroundLine(int row, uint8_t color, int x0, int x1)
{
    const int W  = screenWidthWithBorder;
    const int y0 = borderSize;
    const int y1 = y0 + visibleScreenHeight; // always 200
    if (row < y0 || row >= y1) return;

    uint32_t pix = palette32[color];
    uint32_t* dst = backBuffer.data() + row * W;
    std::fill(dst + x0, dst + x1, pix);
}

void IO::renderBorderLine(int row, uint8_t color, int x0, int x1)
{
    const int W  = screenWidthWithBorder;
    if (row < 0 || row >= screenHeightWithBorder) return;

    const int y0 = borderSize;
    const int y1 = y0 + visibleScreenHeight;

    uint32_t* dst = backBuffer.data() + row * W;
    uint32_t pix  = palette32[color];

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
    {
        return;
    }
    backBuffer[y * screenWidthWithBorder + x] = palette32[colorIndex & 0x0F];
}

void IO::setPixel(int x, int y, uint8_t colorIndex, int hardwareX)
{
    // apply hardware shift once
    int shiftedX = x - hardwareX;
    if (shiftedX < 0 || shiftedX >= screenWidthWithBorder ||
        y < 0 || y >= screenHeightWithBorder)
    {
        return;
    }

    backBuffer[y * screenWidthWithBorder + shiftedX] =
        palette32[colorIndex & 0x0F];
}

SDL_Color IO::getColor(uint8_t colorCode)
{
    static const SDL_Color colors[16] =
    {
        {0, 0, 0},        // 0: Black
        {255, 255, 255},  // 1: White
        {171, 49, 38},      // 2: Red
        {102, 218, 255},  // 3: Cyan
        {187, 63, 184},   // 4: Purple
        {85, 206, 88},     // 5: Green
        {0, 0, 170},      // 6: Blue
        {234, 245, 124},  // 7: Yellow
        {221, 136, 85},   // 8: Orange
        {102, 68, 0},     // 9: Brown
        {255, 119, 119},  // 10: Light Red
        {51, 51, 51},     // 11: Dark Gray
        {119, 119, 119},  // 12: Gray
        {170, 255, 102},  // 13: Light Green
        {0, 136, 255},    // 14: Light Blue
        {187, 187, 187}   // 15: Light Gray
    };

    // Ensure colorCode is within valid range
    if (colorCode > 15)
    {
        return colors[0]; // Default to black for invalid color codes
    }
    return colors[colorCode];
}

void IO::swapBuffer()
{
    uint32_t* drawData = backBuffer.data();
    backBuffer.swap(frontBuffer);
    readyBuffer.store(drawData, std::memory_order_release);
    std::lock_guard lk(qMut);
    qCond.notify_one();
}

void IO::renderLoop(std::atomic<bool>& running)
{
    uint32_t* lastBuf = nullptr;

    while (running.load())
    {
        drainEvents([&](const SDL_Event& e){
            ImGui_ImplSDL2_ProcessEvent(const_cast<SDL_Event*>(&e)); // feed Dear ImGui

            ImGuiIO& io = ImGui::GetIO();
            if (inputCallback) {
                const bool kb_ok = !io.WantCaptureKeyboard || (e.type != SDL_KEYDOWN && e.type != SDL_KEYUP);
                const bool ms_ok = !io.WantCaptureMouse    || (e.type < SDL_MOUSEMOTION || e.type > SDL_MOUSEWHEEL);
                if (kb_ok && ms_ok) inputCallback(e);
            }
            if (e.type == SDL_QUIT) { running = false; }
        });

        if (auto buf = readyBuffer.exchange(nullptr, std::memory_order_acquire))
            lastBuf = buf;

        {
            std::lock_guard<std::mutex> lk(renderMut);

            ImGui_ImplSDLRenderer2_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();
            if (guiCallback) guiCallback();
            ImGui::Render();

            if (lastBuf && screenTexture)
            {
                const int pitch = screenWidthWithBorder * sizeof(uint32_t);
                SDL_Rect dstRect = {
                    0, 0,
                    screenWidthWithBorder * SCALE,
                    screenHeightWithBorder * SCALE
                };

                SDL_UpdateTexture(screenTexture, nullptr, lastBuf, pitch);
                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, screenTexture, nullptr, &dstRect);
                ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
                SDL_RenderPresent(renderer);
            }
        }

        SDL_Delay(1);
    }
}

void IO::finishFrameAndSignal()
{
    {
        std::lock_guard<std::mutex> lk(renderMut);
        backBuffer.swap(frontBuffer);
        readyBuffer.store(frontBuffer.data(), std::memory_order_release);
    }
    std::lock_guard lk(qMut);
    qCond.notify_one();
}

void IO::setScreenDimensions(int visibleW, int visibleH, int border)
{
    std::lock_guard<std::mutex> lk(renderMut);

    visibleScreenWidth = visibleW;
    visibleScreenHeight = visibleH;
    borderSize = border;
    screenWidthWithBorder = visibleW + 2 * borderSize;
    screenHeightWithBorder = visibleH + 2 * borderSize;

    // Resize based on new Video Mode
    frontBuffer.resize(screenWidthWithBorder * screenHeightWithBorder);
    backBuffer.resize(screenWidthWithBorder * screenHeightWithBorder);

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
}

void IO::enqueueEvent(const SDL_Event& e)
{
    std::lock_guard<std::mutex> lk(evMut);
    evQueue.push_back(e);
}

void IO::drainEvents(std::function<void(const SDL_Event&)> consumer)
{
    std::deque<SDL_Event> local;
    {
        std::lock_guard<std::mutex> lk(evMut);
        local.swap(evQueue);
    }
    for (const SDL_Event& e : local) consumer(e);
}
