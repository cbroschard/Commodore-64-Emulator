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
        using UInt32Fn          = std::function<uint32_t()>;
        using SetUInt32Fn       = std::function<void(uint32_t)>;

        UIBridge(EmulatorUI& ui,
         MediaManager* media,
         InputManager* input,
         std::atomic<bool>& uiPaused,
         std::atomic<bool>& running,
         VoidFn attachVirtualModem,
         VoidFn detachVirtualModem,
         BoolFn isVirtualModemAttached,
         BoolFn isVirtualModemOnline,
         SetUInt32Fn setRS232Baud,
         UInt32Fn getRS232Baud,
         VoidFn enableSwiftLink,
         VoidFn disableSwiftLink,
         BoolFn isSwiftLinkEnabled,
         SetUInt32Fn setSwiftLinkBaseAddress,
         UInt32Fn getSwiftLinkBaseAddress,
         VoidFn attachSwiftLinkVirtualModem,
         VoidFn detachSwiftLinkVirtualModem,
         BoolFn isSwiftLinkVirtualModemAttached,
         BoolFn isSwiftLinkVirtualModemOnline,
         VoidFn enableTurbo232,
         VoidFn disableTurbo232,
         BoolFn isTurbo232Enabled,
         SetUInt32Fn setTurbo232BaseAddress,
         UInt32Fn getTurbo232BaseAddress,
         VoidFn attachTurbo232VirtualModem,
         VoidFn detachTurbo232VirtualModem,
         BoolFn isTurbo232VirtualModemAttached,
         BoolFn isTurbo232VirtualModemOnline,
         StringFn saveState,
         StringFn loadState,
         VoidFn warmReset,
         VoidFn coldReset,
         StringFn setSIDModel,
         StringFn setVideoMode,
         VoidFn enterMonitor,
         BoolFn isPal,
         BoolFn is8580,
         BoolFn isMonitorOpen);

        virtual ~UIBridge();

        EmulatorUI::MediaViewState buildMediaViewState() const;
        void processCommands();

        void setMedia(MediaManager* m) { media_ = m; }
        void setInput(InputManager* i) { input_ = i; }

        void toggleManualPause();
        void setManualPause(bool paused);
        bool isManuallyPaused() const { return manualPaused_; }

    private:
        EmulatorUI& ui_;
        MediaManager* media_;
        InputManager* input_;

        std::atomic<bool>& uiPaused_;
        std::atomic<bool>& running_;

        VoidFn attachVirtualModem_;
        VoidFn detachVirtualModem_;
        BoolFn isVirtualModemAttached_;
        BoolFn isVirtualModemOnline_;
        SetUInt32Fn setRS232Baud_;
        UInt32Fn getRS232Baud_;

        VoidFn enableSwiftLink_;
        VoidFn disableSwiftLink_;
        BoolFn isSwiftLinkEnabled_;

        SetUInt32Fn setSwiftLinkBaseAddress_;
        UInt32Fn getSwiftLinkBaseAddress_;

        VoidFn attachSwiftLinkVirtualModem_;
        VoidFn detachSwiftLinkVirtualModem_;
        BoolFn isSwiftLinkVirtualModemAttached_;
        BoolFn isSwiftLinkVirtualModemOnline_;

        VoidFn enableTurbo232_;
        VoidFn disableTurbo232_;
        BoolFn isTurbo232Enabled_;

        SetUInt32Fn setTurbo232BaseAddress_;
        UInt32Fn getTurbo232BaseAddress_;

        VoidFn attachTurbo232VirtualModem_;
        VoidFn detachTurbo232VirtualModem_;
        BoolFn isTurbo232VirtualModemAttached_;
        BoolFn isTurbo232VirtualModemOnline_;

        StringFn saveState_;
        StringFn loadState_;
        VoidFn warmReset_;
        VoidFn coldReset_;
        StringFn setSIDModel_;
        StringFn setVideoMode_;
        VoidFn enterMonitor_;
        BoolFn isPal_;
        BoolFn is8580_;

        bool manualPaused_;
        bool dialogPaused_;
        BoolFn isMonitorOpen_;

        void refreshPauseState();
};

#endif // UIBRIDGE_H
