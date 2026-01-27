#ifndef UIBRIDGE_H
#define UIBRIDGE_H

#include <atomic>
#include <functional>
#include <string>
#include "EmulatorUI.h"

class MediaManager;
class InputManager;

class UIBridge
{
    public:
        using VoidFn            = std::function<void()>;
        using SetVideoModeFn    = std::function<void(const std::string&)>;
        using BoolFn            = std::function<bool()>;

        UIBridge(EmulatorUI& ui,
             MediaManager* media,
             InputManager* input,
             std::atomic<bool>& uiPaused,
             std::atomic<bool>& running,
             VoidFn warmReset,
             VoidFn coldReset,
             SetVideoModeFn setVideoMode,
             VoidFn enterMonitor,
             BoolFn isPal);
        virtual ~UIBridge();

        EmulatorUI::MediaViewState buildMediaViewState() const;
        void processCommands();

        void setMedia(MediaManager* m) { media_ = m; }
        void setInput(InputManager* i) { input_ = i; }

    protected:

    private:
        EmulatorUI& ui_;
        MediaManager* media_;
        InputManager* input_;

        std::atomic<bool>& uiPaused_;
        std::atomic<bool>& running_;

        VoidFn warmReset_;
        VoidFn coldReset_;
        SetVideoModeFn setVideoMode_;
        VoidFn enterMonitor_;
        BoolFn isPal_;
};

#endif // UIBRIDGE_H
