// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "SDLMonitorWindow.h"
#include <algorithm>
#include <cstring>

// Color Definitions
static const SDL_Color COL_TEXT   = {160, 160, 255, 255}; // C64 Light Blue
static const SDL_Color COL_PROMPT = {100, 255, 100, 255}; // Green
static const SDL_Color COL_ERROR  = {255, 80,  80,  255}; // Red
static const SDL_Color COL_HEADER = {255, 255, 255, 255}; // White

static const uint8_t font8x8_basic[96][8] =
{
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // space
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, // !
    {0x66,0x66,0x22,0x00,0x00,0x00,0x00,0x00}, // "
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, // #
    {0x08,0x1E,0x28,0x1C,0x0A,0x3C,0x08,0x00}, // $
    {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, // %
    {0x1C,0x36,0x1C,0x3B,0x33,0x36,0x1C,0x00}, // &
    {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00}, // '

    {0x18,0x30,0x60,0x60,0x60,0x30,0x18,0x00}, // (
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, // )

    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, // *
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00}, // +
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, // ,
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}, // -
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, // .
    {0x00,0x06,0x0C,0x18,0x30,0x60,0x00,0x00}, // /

    {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00}, // 0
    {0x18,0x18,0x38,0x18,0x18,0x18,0x3C,0x00}, // 1
    {0x3C,0x66,0x06,0x0C,0x30,0x60,0x7E,0x00}, // 2
    {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00}, // 3
    {0x06,0x0E,0x1E,0x66,0x7F,0x06,0x06,0x00}, // 4
    {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00}, // 5
    {0x3C,0x66,0x60,0x7C,0x66,0x66,0x3C,0x00}, // 6
    {0x7E,0x66,0x06,0x0C,0x18,0x18,0x18,0x00}, // 7
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00}, // 8
    {0x3C,0x66,0x66,0x3E,0x06,0x66,0x3C,0x00}, // 9

    {0x00,0x00,0x18,0x00,0x00,0x18,0x00,0x00}, // :
    {0x00,0x00,0x18,0x00,0x00,0x18,0x18,0x30}, // ;
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, // <
    {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00}, // =
    {0x60,0x30,0x18,0x0C,0x18,0x30,0x60,0x00}, // >
    {0x3C,0x66,0x06,0x0C,0x18,0x00,0x18,0x00}, // ?
    {0x3C,0x66,0x6E,0x6E,0x60,0x62,0x3C,0x00}, // @

    {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00}, // A
    {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00}, // B
    {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00}, // C
    {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00}, // D
    {0x7E,0x60,0x60,0x78,0x60,0x60,0x7E,0x00}, // E
    {0x7E,0x60,0x60,0x78,0x60,0x60,0x60,0x00}, // F
    {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0x00}, // G
    {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00}, // H
    {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // I
    {0x1E,0x0C,0x0C,0x0C,0x0C,0xCC,0x78,0x00}, // J
    {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00}, // K
    {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00}, // L
    {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00}, // M
    {0x66,0x76,0x7F,0x7F,0x6E,0x66,0x66,0x00}, // N
    {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, // O
    {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00}, // P
    {0x3C,0x66,0x66,0x66,0x6A,0x6C,0x36,0x00}, // Q
    {0x7C,0x66,0x66,0x7C,0x78,0x6C,0x66,0x00}, // R
    {0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00}, // S
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, // T
    {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, // U
    {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00}, // V
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, // W
    {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00}, // X
    {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00}, // Y
    {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00}, // Z

    {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00}, // [
    {0x00,0x60,0x30,0x18,0x0C,0x06,0x00,0x00}, // backslash
    {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00}, // ]
    {0x18,0x3C,0x7E,0x18,0x18,0x18,0x18,0x00}, // ^
    {0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00}, // _
    {0x18,0x0C,0x00,0x00,0x00,0x00,0x00,0x00}, // `

    {0x00,0x00,0x3C,0x06,0x3E,0x66,0x3E,0x00}, // a
    {0x60,0x60,0x7C,0x66,0x66,0x66,0x7C,0x00}, // b
    {0x00,0x00,0x3C,0x60,0x60,0x60,0x3C,0x00}, // c
    {0x06,0x06,0x3E,0x66,0x66,0x66,0x3E,0x00}, // d
    {0x00,0x00,0x3C,0x66,0x7E,0x60,0x3C,0x00}, // e
    {0x1C,0x30,0x7C,0x30,0x30,0x30,0x30,0x00}, // f
    {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x3C}, // g
    {0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x00}, // h
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00}, // i
    {0x06,0x00,0x06,0x06,0x06,0x06,0x06,0x3C}, // j
    {0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0x00}, // k
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // l
    {0x00,0x00,0x66,0x7F,0x7F,0x6B,0x63,0x00}, // m
    {0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x00}, // n
    {0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x00}, // o
    {0x00,0x00,0x7C,0x66,0x66,0x7C,0x60,0x60}, // p
    {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x06}, // q
    {0x00,0x00,0x5C,0x66,0x60,0x60,0x60,0x00}, // r
    {0x00,0x00,0x3E,0x60,0x3C,0x06,0x7C,0x00}, // s
    {0x30,0x30,0x78,0x30,0x30,0x30,0x1C,0x00}, // t
    {0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x00}, // u
    {0x00,0x00,0x66,0x66,0x66,0x3C,0x18,0x00}, // v
    {0x00,0x00,0x63,0x6B,0x7F,0x3E,0x36,0x00}, // w
    {0x00,0x00,0x66,0x3C,0x18,0x3C,0x66,0x00}, // x
    {0x00,0x00,0x66,0x66,0x66,0x3E,0x0C,0x78}, // y
    {0x00,0x00,0x7E,0x0C,0x18,0x30,0x7E,0x00}, // z

    {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00}, // {
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, // |
    {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00}, // }
    {0x3B,0x6E,0x00,0x00,0x00,0x00,0x00,0x00}, // ~
    {0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x00}  // DEL/block
};

SDLMonitorWindow::SDLMonitorWindow() :
    win(nullptr),
    ren(nullptr),
    fontTex(nullptr),
    width(900),
    height(550),
    charWidth(8),
    charHeight(8),
    lineHeight(10),
    padding(5),
    opened(false),
    historyIndex(0),
    scrollOffset(0),
    autoScroll(true),
    selecting(false),
    selAnchor(-1),
    selStart(-1),
    selEnd(-1),
    draggingThumb(false),
    thumbDragGrabY(0),
    cursorPos(0)
{

}

SDLMonitorWindow::~SDLMonitorWindow()
{
    close();
}

void SDLMonitorWindow::createFontTexture()
{
    if (!ren)
        return;

    SDL_Surface* surf = SDL_CreateSurface(96 * 8, 8, SDL_PIXELFORMAT_RGBA32);

    if (!surf)
        return;

    if (!SDL_LockSurface(surf))
    {
        SDL_DestroySurface(surf);
        return;
    }

    auto* pixels = static_cast<uint32_t*>(surf->pixels);

    for (int c = 0; c < 96; ++c)
    {
        for (int y = 0; y < 8; ++y)
        {
            const uint8_t row = font8x8_basic[c][y];

            for (int x = 0; x < 8; ++x)
            {
                const bool pixelOn = ((row >> (7 - x)) & 1) != 0;
                const int surfX = c * 8 + x;
                const int surfY = y;

                pixels[surfY * (surf->pitch / 4) + surfX] = pixelOn ? 0xFFFFFFFF : 0x00000000;
            }
        }
    }

    SDL_UnlockSurface(surf);

    fontTex = SDL_CreateTextureFromSurface(ren, surf);
    SDL_DestroySurface(surf);

    if (!fontTex)
        return;

    SDL_SetTextureBlendMode(fontTex, SDL_BLENDMODE_BLEND);
}

bool SDLMonitorWindow::open(const char* title, int w, int h, ExecFn exec, PromptFn prompt)
{
    if (opened) return true;

    width = w;
    height = h;

    execFn = std::move(exec);
    promptFn = std::move(prompt);

    win = SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE);
    if (!win) return false;

    ren = SDL_CreateRenderer(win, nullptr);
    if (!ren)
    {
        SDL_DestroyWindow(win);
        win = nullptr;
        return false;
    }

    if (!SDL_SetRenderVSync(ren, 1)) SDL_Log("Unable to enable VSync: %s", SDL_GetError());

    createFontTexture();

    if (!fontTex)
    {
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);

        ren = nullptr;
        win = nullptr;

        return false;
    }

    updateLayoutMetrics();

    opened = true;
    lines.clear();
    input.clear();
    cursorPos = 0;

    historyIndex = history.size();

    scrollOffset = 0;

    // Enables SDL_EVENT_TEXT_INPUT events for typing
    if (!SDL_StartTextInput(win))
        SDL_Log("Unable to start text input: %s", SDL_GetError());

    appendLine("ML Monitor - type 'help' and press Enter", COL_HEADER);
    appendLine("------------------------------------------", COL_HEADER);

    return true;
}

void SDLMonitorWindow::close()
{
    if (!opened) return;

    SDL_StopTextInput(win);

    if (fontTex) SDL_DestroyTexture(fontTex);
    fontTex = nullptr;

    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    ren = nullptr;
    win = nullptr;
    opened = false;
}

std::string SDLMonitorWindow::currentPrompt() const
{
    if (promptFn)
        return promptFn();

    return "> ";
}

void SDLMonitorWindow::appendLine(const std::string& s)
{
    appendLine(s, COL_TEXT);
}

void SDLMonitorWindow::appendLine(const std::string& s, SDL_Color color)
{
    lines.push_back({s, color});

    // If we're pinned to bottom, stay pinned. If user scrolled up, do not yank them down.
    if (scrollOffset == 0)
        autoScroll = true;

    if (autoScroll)
        scrollOffset = 0;
}

void SDLMonitorWindow::addChar(char c)
{
    if (c < 32 || c > 126)
        return;

    if (cursorPos < 0)
        cursorPos = 0;
    if (cursorPos > (int)input.size())
        cursorPos = (int)input.size();

    input.insert(input.begin() + cursorPos, c);
    cursorPos++;
}

void SDLMonitorWindow::backspace()
{
    if (cursorPos <= 0 || input.empty())
        return;

    input.erase(input.begin() + (cursorPos - 1));
    cursorPos--;
}

void SDLMonitorWindow::deleteChar()
{
    if (cursorPos < 0 || cursorPos >= (int)input.size())
        return;

    input.erase(input.begin() + cursorPos);
}

void SDLMonitorWindow::moveCursorLeft()
{
    if (cursorPos > 0)
        cursorPos--;
}

void SDLMonitorWindow::moveCursorRight()
{
    if (cursorPos < (int)input.size())
        cursorPos++;
}

void SDLMonitorWindow::moveCursorHome()
{
    cursorPos = 0;
}

void SDLMonitorWindow::moveCursorEnd()
{
    cursorPos = (int)input.size();
}

void SDLMonitorWindow::submitCommand()
{
    // Echo command in Green
    appendLine(currentPrompt() + input, COL_PROMPT);

    // Add to history if not empty
    if (!input.empty())
    {
        // Optional: don't add duplicates if same as last command
        if (history.empty() || history.back() != input)
        {
            history.push_back(input);
        }
    }

    historyIndex = history.size(); // point to new blank line at end

    std::string out;

    // IMPORTANT:
    // Even a blank line must be submitted, because interactive assembler mode
    // uses blank Enter to exit.
    if (execFn)
        out = execFn(input);

    // Split output into lines
    size_t start = 0;
    while (!out.empty() && start < out.size())
    {
        size_t nl = out.find('\n', start);
        std::string sub;

        if (nl == std::string::npos)
        {
            sub = out.substr(start);
            start = out.size();
        }
        else
        {
            sub = out.substr(start, nl - start);
            start = nl + 1;
        }

        // Simple heuristic for error coloring
        SDL_Color lineColor = COL_TEXT;
        if (sub.rfind("Error", 0) == 0 ||
            sub.rfind("Unable", 0) == 0 ||
            sub.rfind("Assembly error", 0) == 0)
        {
            lineColor = COL_ERROR;
        }

        appendLine(sub, lineColor);

        if (start >= out.size())
            break;
    }

    input.clear();
    scrollOffset = 0;
    cursorPos = 0;
}

void SDLMonitorWindow::handleEvent(const SDL_Event& e)
{
    if (!opened)
        return;

    const SDL_WindowID myId = SDL_GetWindowID(win);

    // Handle window close.
    if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == myId)
    {
        close();
        return;
    }

    // Handle resize-related events.
    if ((e.type == SDL_EVENT_WINDOW_RESIZED || e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || e.type == SDL_EVENT_WINDOW_MAXIMIZED) &&
        e.window.windowID == myId)
    {
        SDL_GetWindowSize(win, &width, &height);
        updateLayoutMetrics();

        scrollOffset = clampScrollOffset(scrollOffset);
        autoScroll = (scrollOffset == 0);
        return;
    }

    // Filter events so this monitor only handles events for its own window.
    if ((e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) && e.key.windowID != myId)
        return;

    if (e.type == SDL_EVENT_TEXT_INPUT && e.text.windowID != myId)
        return;

    if (e.type == SDL_EVENT_MOUSE_WHEEL && e.wheel.windowID != myId)
        return;

    if ((e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) && e.button.windowID != myId)
        return;

    if (e.type == SDL_EVENT_MOUSE_MOTION && e.motion.windowID != myId)
        return;

    // Text input.
    if (e.type == SDL_EVENT_TEXT_INPUT)
    {
        for (const char* p = e.text.text; *p; ++p)
            addChar(*p);

        return;
    }

    // Key down.
    if (e.type == SDL_EVENT_KEY_DOWN)
    {
        const SDL_Keymod mods = SDL_GetModState();
        const bool ctrl = (mods & SDL_KMOD_CTRL) != 0;

        // Ctrl+V: paste.
        if (ctrl && e.key.key == SDLK_V)
        {
            char* clip = SDL_GetClipboardText();

            if (clip)
            {
                // Normalize CRLF to LF by ignoring '\r'.
                for (const char* p = clip; *p; ++p)
                {
                    const char c = *p;

                    if (c == '\r')
                        continue;

                    if (c == '\n')
                    {
                        submitCommand();
                        clearSelection();
                    }
                    else
                        addChar(c);
                }

                SDL_free(clip);
            }

            return;
        }

        // Ctrl+C: copy selected output or current input.
        if (ctrl && e.key.key == SDLK_C)
        {
            if (hasSelection())
            {
                const std::string text = getSelectedText();
                SDL_SetClipboardText(text.c_str());
            }
            else
                SDL_SetClipboardText(input.c_str());

            return;
        }

        switch (e.key.key)
        {
            case SDLK_BACKSPACE:
                backspace();
                break;

            case SDLK_LEFT:
                moveCursorLeft();
                break;

            case SDLK_RIGHT:
                moveCursorRight();
                break;

            case SDLK_HOME:
                moveCursorHome();
                break;

            case SDLK_END:
                moveCursorEnd();
                break;

            case SDLK_DELETE:
                deleteChar();
                break;

            case SDLK_RETURN:
            case SDLK_KP_ENTER:
            {
                if (hasSelection())
                {
                    const std::string text = getSelectedText();
                    SDL_SetClipboardText(text.c_str());
                    clearSelection();
                }
                else
                {
                    submitCommand();
                    clearSelection();
                }

                break;
            }

            case SDLK_ESCAPE:
                close();
                break;

            case SDLK_PAGEUP:
                scrollOffset -= 10;
                scrollOffset = clampScrollOffset(scrollOffset);
                autoScroll = (scrollOffset == 0);
                break;

            case SDLK_PAGEDOWN:
                scrollOffset += 10;
                scrollOffset = clampScrollOffset(scrollOffset);
                autoScroll = (scrollOffset == 0);
                break;

            // Command history navigation.
            case SDLK_UP:
            {
                if (historyIndex > 0)
                {
                    historyIndex--;
                    input = history[historyIndex];
                    cursorPos = static_cast<int>(input.size());
                }

                break;
            }

            case SDLK_DOWN:
            {
                if (historyIndex < static_cast<int>(history.size()))
                {
                    historyIndex++;

                    if (historyIndex == static_cast<int>(history.size()))
                        input.clear();
                    else
                        input = history[historyIndex];

                    cursorPos = static_cast<int>(input.size());
                }

                break;
            }

            default:
                break;
        }

        return;
    }

    // Mouse-wheel scrolling.
    if (e.type == SDL_EVENT_MOUSE_WHEEL)
    {
        if (e.wheel.y > 0.0f)
            scrollOffset -= 3;

        if (e.wheel.y < 0.0f)
            scrollOffset += 3;

        scrollOffset = clampScrollOffset(scrollOffset);
        autoScroll = (scrollOffset == 0);
        return;
    }

    // Mouse button down: selection or scrollbar interaction.
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT)
    {
        const int mouseX = static_cast<int>(e.button.x);
        const int mouseY = static_cast<int>(e.button.y);
        const SDL_Point point{mouseX, mouseY};

        // Begin dragging the scrollbar thumb.
        SDL_Rect thumb = getScrollbarThumbRect();

        if (SDL_PointInRect(&point, &thumb))
        {
            draggingThumb = true;
            thumbDragGrabY = mouseY - thumb.y;
            return;
        }

        // Clicked the scrollbar track.
        SDL_Rect track = getScrollbarTrackRect();

        if (SDL_PointInRect(&point, &track))
        {
            setScrollFromThumbCenterY(mouseY);
            draggingThumb = true;
            const SDL_Rect newThumb = getScrollbarThumbRect();
            thumbDragGrabY = newThumb.h / 2;
            return;
        }

        // Begin selecting output lines.
        const int index = lineIndexFromMouseY(mouseY);

        if (index >= 0)
        {
            selecting = true;
            selAnchor = index;
            selStart = index;
            selEnd = index;
        }
        else
            clearSelection();

        return;
    }

    // Mouse motion: drag scrollbar thumb or update selection.
    if (e.type == SDL_EVENT_MOUSE_MOTION)
    {
        const int motionY = static_cast<int>(e.motion.y);

        if (draggingThumb)
        {
            const SDL_Rect thumb = getScrollbarThumbRect();
            const int newThumbY = motionY - thumbDragGrabY;
            const int thumbCenterY = newThumbY + thumb.h / 2;

            setScrollFromThumbCenterY(thumbCenterY);
            return;
        }

        if (selecting)
        {
            int index = lineIndexFromMouseY(motionY);

            if (index < 0 && selAnchor >= 0 && !lines.empty())
            {
                const int inputY = height - padding - lineHeight;
                const int historyBottomY = inputY - lineHeight;
                const int historyAreaBottom = inputY - 1;

                int first;
                int last;

                visibleLineRange(first, last);

                if (motionY < 0)
                    index = first;
                else if (motionY > historyAreaBottom)
                    index = last;
                else
                    index = (motionY < historyBottomY / 2) ? first : last;
            }

            if (index >= 0 && selAnchor >= 0)
            {
                selStart = std::min(selAnchor, index);
                selEnd = std::max(selAnchor, index);
            }

            return;
        }

        return;
    }

    // Mouse button up: stop scrollbar drag or selection.
    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT)
    {
        selecting = false;
        draggingThumb = false;
        return;
    }
}

void SDLMonitorWindow::drawString(int x, int y, const std::string& str, const SDL_Color& color)
{
    if (!fontTex) return;

    SDL_SetTextureColorMod(fontTex, color.r, color.g, color.b);

    SDL_FRect src{0.0f, 0.0f, 8.0f, 8.0f};
    SDL_FRect dst{static_cast<float>(x), static_cast<float>(y), static_cast<float>(charWidth), static_cast<float>(charHeight)};

    for (char c : str)
    {
        int index = (unsigned char)c - 32;
        if (index < 0 || index >= 96) index = 95;

        src.x = static_cast<float>(index * 8);

        SDL_RenderTexture(ren, fontTex, &src, &dst);
        dst.x += static_cast<float>(charWidth);
    }
}

void SDLMonitorWindow::render()
{
    if (!opened || !ren)
        return;

    SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
    SDL_RenderClear(ren);

    const int inputY = height - padding - lineHeight;
    const std::string prompt = currentPrompt();

    drawString(padding, inputY, prompt, COL_PROMPT);
    drawString(padding + static_cast<int>(prompt.length()) * charWidth, inputY, input, COL_TEXT);

    // Blinking cursor.
    if ((SDL_GetTicks() / 500) % 2 == 0)
    {
        const int cursorX = padding + static_cast<int>(prompt.length() + cursorPos) * charWidth;

        SDL_FRect cursorRect{static_cast<float>(cursorX), static_cast<float>(inputY), static_cast<float>(charWidth),
            static_cast<float>(charHeight)};

        SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
        SDL_RenderFillRect(ren, &cursorRect);
    }

    const int historyBottomY = inputY - lineHeight;
    const int historyCount = static_cast<int>(lines.size());
    const int startIdx = historyCount - 1 - scrollOffset;
    int currentY = historyBottomY;

    for (int i = startIdx; i >= 0; --i)
    {
        if (currentY < 0)
            break;

        const bool selected = hasSelection() && i >= selStart && i <= selEnd;

        if (selected)
        {
            SDL_FRect background{static_cast<float>(padding - 2), static_cast<float>(currentY - 1),
                static_cast<float>(width - (padding * 2) - 12), static_cast<float>(lineHeight)};

            SDL_SetRenderDrawColor(ren, 60, 60, 110, 255);
            SDL_RenderFillRect(ren, &background);
        }

        drawString(padding, currentY, lines[i].text, lines[i].color);
        currentY -= lineHeight;
    }

    const int visible = visibleHistoryLines();
    const int total = static_cast<int>(lines.size());

    if (total > visible)
    {
        const SDL_Rect track = getScrollbarTrackRect();
        const SDL_Rect thumb = getScrollbarThumbRect();

        const SDL_FRect trackF{static_cast<float>(track.x), static_cast<float>(track.y), static_cast<float>(track.w),
            static_cast<float>(track.h)};

        const SDL_FRect thumbF{static_cast<float>(thumb.x), static_cast<float>(thumb.y), static_cast<float>(thumb.w),
            static_cast<float>(thumb.h)};

        SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
        SDL_RenderFillRect(ren, &trackF);
        SDL_SetRenderDrawColor(ren, 120, 120, 120, 255);
        SDL_RenderFillRect(ren, &thumbF);
    }

    SDL_RenderPresent(ren);
}

int SDLMonitorWindow::visibleHistoryLines() const
{
    int inputY = height - padding - lineHeight;
    int historyBottomY = inputY - lineHeight;

    int count = (historyBottomY / lineHeight) + 1;
    return std::max(0, count);
}

int SDLMonitorWindow::clampScrollOffset(int off) const
{
    int vis = visibleHistoryLines();
    int maxOff = std::max(0, (int)lines.size() - vis);
    if (off < 0) off = 0;
    if (off > maxOff) off = maxOff;
    return off;
}

void SDLMonitorWindow::clearSelection()
{
    selAnchor = selStart = selEnd = -1;
}

bool SDLMonitorWindow::hasSelection() const
{
    return (selStart >= 0 && selEnd >= 0 && selStart <= selEnd && selEnd < (int)lines.size());
}

std::string SDLMonitorWindow::getSelectedText() const
{
    if (!hasSelection()) return "";

    std::string out;
    for (int i = selStart; i <= selEnd; ++i)
    {
        out += lines[i].text;
        out += "\n";
    }
    return out;
}

int SDLMonitorWindow::lineIndexFromMouseY(int mouseY) const
{
    const int inputY = height - padding - lineHeight;

    // History occupies y = 0 .. inputY-1 (everything above the input line)
    const int historyAreaBottom = inputY - 1;

    // This is the y where the LAST visible history line starts (top of that line)
    const int historyBottomY = inputY - lineHeight;

    if (mouseY < 0 || mouseY > historyAreaBottom)
        return -1;

    if (mouseY > historyBottomY)
        mouseY = historyBottomY;

    const int rowFromBottom = (historyBottomY - mouseY) / lineHeight;

    const int historyCount = (int)lines.size();
    const int startIdx = historyCount - 1 - scrollOffset;

    const int idx = startIdx - rowFromBottom;
    if (idx < 0 || idx >= historyCount)
        return -1;

    return idx;
}

SDL_Rect SDLMonitorWindow::getScrollbarTrackRect() const
{
    int inputY = height - padding - lineHeight;
    int historyBottomY = inputY - lineHeight;

    SDL_Rect r;
    r.w = 10;
    r.x = width - r.w - 2;
    r.y = 2;
    r.h = std::max(0, historyBottomY - 2);
    return r;
}

SDL_Rect SDLMonitorWindow::getScrollbarThumbRect() const
{
    SDL_Rect track = getScrollbarTrackRect();

    int vis = visibleHistoryLines();
    int total = (int)lines.size();
    if (total <= vis || track.h <= 0)
        return SDL_Rect{track.x, track.y, track.w, track.h};

    float fracVisible = (float)vis / (float)total;
    int thumbH = std::max(16, (int)(track.h * fracVisible));

    int maxOff = std::max(1, total - vis);

    // Internal scrollOffset:
    //   0      = bottom/newest
    //   maxOff = top/oldest
    //
    // UI scrollbar:
    //   bottom of track = bottom/newest
    //   top of track    = top/oldest
    //
    // So invert the fraction.
    float fracScroll = 1.0f - ((float)scrollOffset / (float)maxOff);

    int travel = track.h - thumbH;
    int thumbY = track.y + (int)(fracScroll * travel);

    return SDL_Rect{track.x, thumbY, track.w, thumbH};
}

void SDLMonitorWindow::setScrollFromThumbCenterY(int thumbCenterY)
{
    SDL_Rect track = getScrollbarTrackRect();
    int vis = visibleHistoryLines();
    int total = (int)lines.size();
    if (total <= vis || track.h <= 0) return;

    int maxOff = std::max(1, total - vis);
    SDL_Rect thumb = getScrollbarThumbRect();
    int thumbH = thumb.h;

    int travel = track.h - thumbH;
    if (travel <= 0) return;

    int desiredThumbY = thumbCenterY - (thumbH / 2);
    desiredThumbY = std::clamp(desiredThumbY, track.y, track.y + travel);

    // UI:
    //   top of track    => oldest => maxOff
    //   bottom of track => newest => 0
    //
    // So invert the mapping.
    float frac = (float)(desiredThumbY - track.y) / (float)travel;
    scrollOffset = clampScrollOffset((int)((1.0f - frac) * maxOff + 0.5f));

    autoScroll = (scrollOffset == 0);
}

void SDLMonitorWindow::visibleLineRange(int& first, int& last) const
{
    const int historyCount = (int)lines.size();
    if (historyCount <= 0) { first = last = -1; return; }

    const int vis = visibleHistoryLines();

    // bottom-most visible line index (the one drawn at historyBottomY)
    last = historyCount - 1 - scrollOffset;
    last = std::clamp(last, 0, historyCount - 1);

    // top-most visible line index
    first = std::max(0, last - (vis - 1));
}

void SDLMonitorWindow::updateLayoutMetrics()
{
    // Base monitor design target
    const int targetCols = 80;
    const int targetRows = 32;

    // Compute integer scale factors so bitmap font stays sharp
    int scaleX = std::max(1, width / (targetCols * 8));
    int scaleY = std::max(1, height / (targetRows * 10));

    // Use the smaller scale to preserve aspect ratio
    int scale = std::max(1, std::min(scaleX, scaleY));

    scale = std::min(scale, 6);

    charWidth  = 8 * scale;
    charHeight = 8 * scale;

    // Extra spacing between lines
    lineHeight = charHeight + std::max(2, scale * 2);

    // Scale padding a little too
    padding = std::max(5, scale * 3);
}
