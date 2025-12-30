#pragma once
#include <functional>
#include <string>
#include <vector>
#include "sdl2/SDL.h"

class SDLMonitorWindow
{
public:
    using ExecFn = std::function<std::string(const std::string& cmd)>; // returns output to show

    SDLMonitorWindow();
    ~SDLMonitorWindow();

    bool isOpen() const { return opened; }
    bool open(const char* title, int w, int h, ExecFn exec);
    void close();

    // Call from your existing event pump (drainEvents consumer)
    void handleEvent(const SDL_Event& e);

    // Call once per frame from render loop
    void render();

    // Allow external push (optional)
    void appendLine(const std::string& s);

private:
    void submitCommand();
    void backspace();
    void addChar(char c);

    SDL_Window*   win = nullptr;
    SDL_Renderer* ren = nullptr;

    bool opened = false;
    ExecFn execFn;

    std::vector<std::string> lines;
    std::string input;
    int scrollOffset = 0; // simplest scrolling, line-based

    // window dimensions cached
    int width = 900;
    int height = 550;
};
