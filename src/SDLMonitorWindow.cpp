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

static const uint8_t font8x8_basic[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // space
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, // !
    {0x66,0x66,0x22,0x00,0x00,0x00,0x00,0x00}, // "
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, // #
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, // $
    {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, // %
    {0x1C,0x36,0x1C,0x3B,0x33,0x36,0x1C,0x00}, // &
    {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00}, // '
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, // (
    {0x18,0x30,0x60,0x60,0x60,0x30,0x18,0x00}, // )
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, // *
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00}, // +
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, // ,
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}, // -
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, // .
    {0x00,0x60,0x30,0x18,0x0C,0x06,0x00,0x00}, // /
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
    {0x00,0x06,0x0C,0x18,0x30,0x60,0x00,0x00}, // Backslash
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
    {0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x00}  // DEL (block)
};

SDLMonitorWindow::SDLMonitorWindow() :
    win(nullptr),
    ren(nullptr),
    fontTex(nullptr),
    width(900),
    height(550),
    charWidth(8),
    charHeight(8),
    opened(false),
    historyIndex(0),
    scrollOffset(0),
    autoScroll(true),
    maxScrollOffset(0),
    selecting(false),
    selAnchor(-1),
    selStart(-1),
    selEnd(-1),
    draggingThumb(false),
    thumbDragGrabY(0)
{

}

SDLMonitorWindow::~SDLMonitorWindow()
{
    close();
}

void SDLMonitorWindow::createFontTexture()
{
    if (!ren) return;

    // Create a surface first to manipulate pixels easily
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, 96 * 8, 8, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf) return;

    SDL_LockSurface(surf);
    uint32_t* pixels = (uint32_t*)surf->pixels;

    for (int c = 0; c < 96; ++c)
    {
        for (int y = 0; y < 8; ++y)
        {
            uint8_t row = font8x8_basic[c][y];
            for (int x = 0; x < 8; ++x)
            {
                // Check bit (most significant bit is left)
                bool pixelOn = (row >> (7 - x)) & 1;

                int surfX = c * 8 + x;
                int surfY = y;

                // White text, transparent background
                pixels[surfY * (surf->pitch / 4) + surfX] = pixelOn ? 0xFFFFFFFF : 0x00000000;
            }
        }
    }
    SDL_UnlockSurface(surf);

    // Convert to texture
    fontTex = SDL_CreateTextureFromSurface(ren, surf);
    SDL_FreeSurface(surf);

    // Enable blending so transparency works
    SDL_SetTextureBlendMode(fontTex, SDL_BLENDMODE_BLEND);
}

bool SDLMonitorWindow::open(const char* title, int w, int h, ExecFn exec)
{
    if (opened) return true;

    width = w; height = h;
    execFn = std::move(exec);

    win = SDL_CreateWindow(title,
                           SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           width, height,
                           SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!win) return false;

    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren)
    {
        SDL_DestroyWindow(win);
        win = nullptr;
        return false;
    }

    createFontTexture();

    opened = true;
    lines.clear();
    input.clear();

    // Clear history or leave it for persistence?
    // Usually convenient to keep history, but for simplicity we can clear or keep.
    // Let's keep it but reset index.
    historyIndex = history.size();

    scrollOffset = 0;

    // Enables SDL_TEXTINPUT events for typing
    SDL_StartTextInput();

    appendLine("ML Monitor - type 'help' and press Enter", COL_HEADER);
    appendLine("------------------------------------------", COL_HEADER);

    return true;
}

void SDLMonitorWindow::close()
{
    if (!opened) return;

    SDL_StopTextInput();

    if (fontTex) SDL_DestroyTexture(fontTex);
    fontTex = nullptr;

    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    ren = nullptr;
    win = nullptr;
    opened = false;
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
    // Filter non-printable
    if (c >= 32 && c <= 126)
        input.push_back(c);
}

void SDLMonitorWindow::backspace()
{
    if (!input.empty())
        input.pop_back();
}

void SDLMonitorWindow::submitCommand()
{
    // Echo command in Green
    appendLine("> " + input, COL_PROMPT);

    // Add to history if not empty
    if (!input.empty())
    {
        // Optional: don't add duplicates if same as last command
        if (history.empty() || history.back() != input)
        {
            history.push_back(input);
        }
        historyIndex = history.size(); // point to new blank line at end
    }

    std::string out;
    if (execFn && !input.empty())
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
        if (sub.rfind("Error", 0) == 0 || sub.rfind("Unable", 0) == 0)
        {
            lineColor = COL_ERROR;
        }

        appendLine(sub, lineColor);

        if (start >= out.size()) break;
    }

    input.clear();
    scrollOffset = 0;
}

void SDLMonitorWindow::handleEvent(const SDL_Event& e)
{
    if (!opened) return;

    // Handle window close / resize logic
    if (e.type == SDL_WINDOWEVENT)
    {
        if (e.window.windowID == SDL_GetWindowID(win))
        {
            if (e.window.event == SDL_WINDOWEVENT_CLOSE)
            {
                close();
            }
            else if (e.window.event == SDL_WINDOWEVENT_RESIZED)
            {
                width  = e.window.data1;
                height = e.window.data2;

                // After resize, make sure scrollOffset is still valid.
                scrollOffset = clampScrollOffset(scrollOffset);
                autoScroll   = (scrollOffset == 0);
            }
        }
        return;
    }

    // Filter events for THIS window only
    const Uint32 myId = SDL_GetWindowID(win);

    if ((e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) && e.key.windowID != myId) return;
    if (e.type == SDL_TEXTINPUT && e.text.windowID != myId) return;
    if (e.type == SDL_MOUSEWHEEL && e.wheel.windowID != myId) return;

    // IMPORTANT: mouse events must also be filtered or selection/drag will break
    if ((e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) && e.button.windowID != myId) return;
    if (e.type == SDL_MOUSEMOTION && e.motion.windowID != myId) return;

    // Text input (typing)
    if (e.type == SDL_TEXTINPUT)
    {
        for (const char* p = e.text.text; *p; ++p)
            addChar(*p);

        return;
    }

    // Key down
    if (e.type == SDL_KEYDOWN)
    {
        SDL_Keymod mods = SDL_GetModState();
        bool ctrl = (mods & KMOD_CTRL) != 0;

        // Ctrl+V Paste
        if (ctrl && e.key.keysym.sym == SDLK_v)
        {
            char* clip = SDL_GetClipboardText();
            if (clip)
            {
                input += clip;
                SDL_free(clip);
            }
            return;
        }

        // Ctrl+C Copy (selection if exists, otherwise input fallback)
        if (ctrl && e.key.keysym.sym == SDLK_c)
        {
            if (hasSelection())
            {
                std::string txt = getSelectedText();
                SDL_SetClipboardText(txt.c_str());
            }
            else
            {
                SDL_SetClipboardText(input.c_str());
            }
            return;
        }

        switch (e.key.keysym.sym)
        {
            case SDLK_BACKSPACE:
                backspace();
                break;

            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                submitCommand();
                clearSelection(); // optional; feels nicer
                break;

            case SDLK_ESCAPE:
                close();
                break;

            case SDLK_PAGEUP:
                scrollOffset += 10;
                scrollOffset = clampScrollOffset(scrollOffset);
                autoScroll   = (scrollOffset == 0);
                break;

            case SDLK_PAGEDOWN:
                scrollOffset -= 10;
                scrollOffset = clampScrollOffset(scrollOffset);
                autoScroll   = (scrollOffset == 0);
                break;

            // Command history navigation (Up/Down)
            case SDLK_UP:
                if (historyIndex > 0)
                {
                    historyIndex--;
                    input = history[historyIndex];
                }
                break;

            case SDLK_DOWN:
                if (historyIndex < (int)history.size())
                {
                    historyIndex++;
                    if (historyIndex == (int)history.size())
                        input.clear();
                    else
                        input = history[historyIndex];
                }
                break;

            default:
                break;
        }

        return;
    }

    // Mouse wheel scrolling
    if (e.type == SDL_MOUSEWHEEL)
    {
        if (e.wheel.y > 0) scrollOffset += 3;
        if (e.wheel.y < 0) scrollOffset -= 3;

        scrollOffset = clampScrollOffset(scrollOffset);
        autoScroll   = (scrollOffset == 0);

        return;
    }

    // Mouse button down (selection or scrollbar interactions)
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
    {
        SDL_Point p{ e.button.x, e.button.y };

        // If clicked thumb => drag scrollbar
        SDL_Rect thumb = getScrollbarThumbRect();
        if (SDL_PointInRect(&p, &thumb))
        {
            draggingThumb = true;
            thumbDragGrabY = e.button.y - thumb.y;
            return;
        }

        // If clicked track (but not thumb) => jump and begin thumb drag
        SDL_Rect track = getScrollbarTrackRect();
        if (SDL_PointInRect(&p, &track))
        {
            setScrollFromThumbCenterY(e.button.y);

            draggingThumb = true;
            SDL_Rect thumb2 = getScrollbarThumbRect();
            thumbDragGrabY = thumb2.h / 2; // grab center for smooth drag
            return;
        }

        // Start selecting lines
        int idx = lineIndexFromMouseY(e.button.y);
        if (idx >= 0)
        {
            selecting = true;
            selAnchor = idx;
            selStart  = idx;
            selEnd    = idx;
        }
        else
        {
            clearSelection();
        }

        return;
    }

    // Mouse motion (drag thumb or update selection)
    if (e.type == SDL_MOUSEMOTION)
    {
        if (draggingThumb)
        {
            SDL_Rect thumb = getScrollbarThumbRect();
            int newThumbY = e.motion.y - thumbDragGrabY;
            int thumbCenterY = newThumbY + thumb.h / 2;

            setScrollFromThumbCenterY(thumbCenterY);
            return;
        }

        if (selecting)
        {
            int idx = lineIndexFromMouseY(e.motion.y);
            if (idx >= 0 && selAnchor >= 0)
            {
                selStart = std::min(selAnchor, idx);
                selEnd   = std::max(selAnchor, idx);
            }
        }

        return;
    }

    // Mouse button up (stop drag/selection)
    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT)
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

    SDL_Rect src{0, 0, 8, 8}; // 8x8 char
    SDL_Rect dst{x, y, 8, 8};

    for (char c : str)
    {
        // Calculate index in our flat font strip (ASCII 32 to 127)
        int index = (unsigned char)c - 32;
        if (index < 0 || index >= 96) index = 95; // default to block for unknown

        src.x = index * 8;

        SDL_RenderCopy(ren, fontTex, &src, &dst);
        dst.x += 8;
    }
}

void SDLMonitorWindow::render()
{
    if (!opened || !ren) return;

    // Background Color (Deep Dark Grey/Black)
    SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
    SDL_RenderClear(ren);

    // Padding settings
    const int padding = 5;
    const int lineHeight = 10; // 8px char + 2px spacing

    // 1. Render Input Line at bottom
    int inputY = height - padding - lineHeight;

    std::string prompt = "> ";
    drawString(padding, inputY, prompt, COL_PROMPT);
    drawString(padding + (prompt.length() * 8), inputY, input, COL_TEXT);

    // Blinking cursor
    if ((SDL_GetTicks() / 500) % 2 == 0)
    {
        int cursorX = padding + (prompt.length() + input.length()) * 8;
        SDL_Rect cursorRect = { cursorX, inputY, 8, 8 };
        SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
        SDL_RenderFillRect(ren, &cursorRect);
    }

    int historyBottomY = inputY - lineHeight;
    int historyCount = lines.size();

    // We iterate backwards from the end of the list minus scrollOffset
    int startIdx = historyCount - 1 - scrollOffset;

    int currentY = historyBottomY;

    for (int i = startIdx; i >= 0; --i)
    {
        if (currentY < 0) break; // Off top of screen

        bool selected = hasSelection() && (i >= selStart && i <= selEnd);
        if (selected)
        {
            SDL_Rect bg{ padding - 2, currentY - 1, width - padding - 14, lineHeight };
            SDL_SetRenderDrawColor(ren, 60, 60, 110, 255);
            SDL_RenderFillRect(ren, &bg);
        }

        // Now passing the stored color for each line
        drawString(padding, currentY, lines[i].text, lines[i].color);
        currentY -= lineHeight;
    }

    // Scrollbar indicator (simple)
    int vis = visibleHistoryLines();
    int total = (int)lines.size();
    if (total > vis)
    {
        SDL_Rect track = getScrollbarTrackRect();
        SDL_Rect thumb = getScrollbarThumbRect();

        SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
        SDL_RenderFillRect(ren, &track);

        SDL_SetRenderDrawColor(ren, 120, 120, 120, 255);
        SDL_RenderFillRect(ren, &thumb);
    }

        SDL_RenderPresent(ren);
}

int SDLMonitorWindow::visibleHistoryLines() const
{
    const int padding = 5;
    const int lineHeight = 10;

    int inputY = height - padding - lineHeight;
    int historyBottomY = inputY - lineHeight;

    // number of full lines that can fit from y=0..historyBottomY
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
    const int padding = 5;
    const int lineHeight = 10;

    int inputY = height - padding - lineHeight;
    int historyBottomY = inputY - lineHeight;

    if (mouseY < 0 || mouseY > historyBottomY) return -1;

    int rowFromBottom = (historyBottomY - mouseY) / lineHeight;

    // Which line is shown at bottom?
    int historyCount = (int)lines.size();
    int startIdx = historyCount - 1 - scrollOffset;

    int idx = startIdx - rowFromBottom;
    if (idx < 0 || idx >= historyCount) return -1;
    return idx;
}

SDL_Rect SDLMonitorWindow::getScrollbarTrackRect() const
{
    const int padding = 5;
    const int lineHeight = 10;
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
    float fracScroll = (float)scrollOffset / (float)maxOff; // 0..1

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

    float frac = (float)(desiredThumbY - track.y) / (float)travel;
    scrollOffset = clampScrollOffset((int)(frac * maxOff + 0.5f));

    autoScroll = (scrollOffset == 0);
}
