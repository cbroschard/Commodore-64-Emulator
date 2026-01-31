// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "sdl2/SDL.h"

// Struct to hold text and its specific color
struct ConsoleLine
{
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

        void handleEvent(const SDL_Event& e);

        void render();

        // Allow external push (optional) - Overloaded to support colors
        void appendLine(const std::string& s);
        void appendLine(const std::string& s, SDL_Color color);

    protected:

    private:

        SDL_Window* win;
        SDL_Renderer* ren;
        SDL_Texture* fontTex;

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
        bool autoScroll;       // stay pinned to bottom when true
        int  maxScrollOffset;     // computed from visible lines

        bool selecting;
        int  selAnchor;          // line index at mouse-down
        int  selStart;           // inclusive line index
        int  selEnd;             // inclusive line index

        bool draggingThumb;
        int  thumbDragGrabY;

        void submitCommand();
        void backspace();
        void addChar(char c);

        void createFontTexture();
        void drawString(int x, int y, const std::string& str, const SDL_Color& color);

        int  visibleHistoryLines() const;
        int  clampScrollOffset(int off) const;
        int  lineIndexFromMouseY(int mouseY) const;
        void clearSelection();
        bool hasSelection() const;
        std::string getSelectedText() const;

        SDL_Rect getScrollbarTrackRect() const;
        SDL_Rect getScrollbarThumbRect() const;
        void setScrollFromThumbCenterY(int thumbCenterY);
};
