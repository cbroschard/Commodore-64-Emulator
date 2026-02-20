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
        using StringFn          = std::function<void(const std::string&)>;
        using BoolFn            = std::function<bool()>;

        UIBridge(EmulatorUI& ui,
             MediaManager* media,
             InputManager* input,
             std::atomic<bool>& uiPaused,
             std::atomic<bool>& running,
             StringFn saveState,
             StringFn loadState,
             VoidFn warmReset,
             VoidFn coldReset,
             StringFn setVideoMode,
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

        StringFn saveState_;
        StringFn loadState_;
        VoidFn warmReset_;
        VoidFn coldReset_;
        StringFn setVideoMode_;
        VoidFn enterMonitor_;
        BoolFn isPal_;
};

#endif // UIBRIDGE_H
