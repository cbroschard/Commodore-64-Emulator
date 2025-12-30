#include "SDLMonitorWindow.h"
#include <algorithm>

SDLMonitorWindow::SDLMonitorWindow() {}
SDLMonitorWindow::~SDLMonitorWindow() { close(); }

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

    opened = true;
    lines.clear();
    input.clear();
    scrollOffset = 0;

    // Enables SDL_TEXTINPUT events for typing
    SDL_StartTextInput();

    lines.push_back("ML Monitor (SDL) - type 'help' and press Enter");
    lines.push_back("------------------------------------------------");

    return true;
}

void SDLMonitorWindow::close()
{
    if (!opened) return;

    SDL_StopTextInput();

    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    ren = nullptr;
    win = nullptr;
    opened = false;
}

void SDLMonitorWindow::appendLine(const std::string& s)
{
    lines.push_back(s);
    // clamp scroll to bottom
    scrollOffset = 0;
}

void SDLMonitorWindow::addChar(char c)
{
    input.push_back(c);
}

void SDLMonitorWindow::backspace()
{
    if (!input.empty())
        input.pop_back();
}

void SDLMonitorWindow::submitCommand()
{
    if (input.empty())
        return;

    // echo command
    lines.push_back("> " + input);

    std::string out;
    if (execFn)
        out = execFn(input);

    // split output into lines
    size_t start = 0;
    while (!out.empty() && start < out.size())
    {
        size_t nl = out.find('\n', start);
        if (nl == std::string::npos)
        {
            lines.push_back(out.substr(start));
            break;
        }
        lines.push_back(out.substr(start, nl - start));
        start = nl + 1;
    }

    input.clear();
    scrollOffset = 0;
}

void SDLMonitorWindow::handleEvent(const SDL_Event& e)
{
    if (!opened) return;

    // Only handle events for our window
    if (e.type == SDL_WINDOWEVENT)
    {
        if (e.window.windowID == SDL_GetWindowID(win))
        {
            if (e.window.event == SDL_WINDOWEVENT_CLOSE)
                close();
        }
        return;
    }

    // Ignore events not for our window when applicable
    const Uint32 myId = SDL_GetWindowID(win);
    if ((e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) && e.key.windowID != myId) return;
    if (e.type == SDL_TEXTINPUT && e.text.windowID != myId) return;
    if (e.type == SDL_MOUSEWHEEL && e.wheel.windowID != myId) return;

    if (e.type == SDL_TEXTINPUT)
    {
        // ASCII typing (good enough for commands)
        for (const char* p = e.text.text; *p; ++p)
            addChar(*p);
    }
    else if (e.type == SDL_KEYDOWN)
    {
        switch (e.key.keysym.sym)
        {
            case SDLK_BACKSPACE: backspace(); break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:  submitCommand(); break;
            case SDLK_ESCAPE:    close(); break;
            case SDLK_PAGEUP:    scrollOffset = std::min(scrollOffset + 5, (int)lines.size()); break;
            case SDLK_PAGEDOWN:  scrollOffset = std::max(scrollOffset - 5, 0); break;
            default: break;
        }
    }
    else if (e.type == SDL_MOUSEWHEEL)
    {
        // simple scroll
        if (e.wheel.y > 0) scrollOffset = std::min(scrollOffset + 3, (int)lines.size());
        if (e.wheel.y < 0) scrollOffset = std::max(scrollOffset - 3, 0);
    }
}

void SDLMonitorWindow::render()
{
    if (!opened) return;

    // Simple rendering placeholder: colored bars only (no font).
    // We'll add text rendering with SDL_ttf next if you want.
    SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
    SDL_RenderClear(ren);

    // Output area
    SDL_Rect outRect{ 10, 10, width - 20, height - 60 };
    SDL_SetRenderDrawColor(ren, 35, 35, 35, 255);
    SDL_RenderFillRect(ren, &outRect);

    // Input area
    SDL_Rect inRect{ 10, height - 45, width - 20, 35 };
    SDL_SetRenderDrawColor(ren, 50, 50, 50, 255);
    SDL_RenderFillRect(ren, &inRect);

    // caret indicator (since we aren't drawing text yet)
    SDL_Rect caret{ 20, height - 38, 8, 20 };
    SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
    SDL_RenderFillRect(ren, &caret);

    SDL_RenderPresent(ren);
}
