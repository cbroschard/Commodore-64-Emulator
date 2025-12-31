#pragma once
#include <functional>
#include <string>
#include <vector>
#include "sdl2/SDL.h"

// Struct to hold text and its specific color
struct ConsoleLine {
    std::string text;
    SDL_Color color;
};

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

        // Allow external push (optional) - Overloaded to support colors
        void appendLine(const std::string& s);
        void appendLine(const std::string& s, SDL_Color color);

    protected:

    private:

        SDL_Window* win = nullptr;
        SDL_Renderer* ren = nullptr;

        SDL_Texture* fontTex = nullptr;

        // window dimensions cached
        int width;
        int height;

        // Font constants
        const int charWidth;
        const int charHeight;

        bool opened;
        ExecFn execFn;

        // Output buffer
        std::vector<ConsoleLine> lines;

        // Input handling
        std::string input;

        // History handling
        std::vector<std::string> history;
        int historyIndex; // Points to current history item (or history.size() for new line)

        int scrollOffset;

        void submitCommand();
        void backspace();
        void addChar(char c);

        void createFontTexture();
        void drawString(int x, int y, const std::string& str, const SDL_Color& color);
};
