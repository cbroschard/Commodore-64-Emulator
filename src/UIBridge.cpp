#include <SDL2/sdl.h>
#include "InputManager.h"
#include "MediaManager.h"
#include "UIBridge.h"

UIBridge::UIBridge(EmulatorUI& ui,
            MediaManager* media,
            InputManager* input,
            std::atomic<bool>& uiPaused,
            std::atomic<bool>& running,
            UIBridge::VoidFn warmReset,
            UIBridge::VoidFn coldReset,
            UIBridge::SetVideoModeFn setVideoMode,
            UIBridge::VoidFn enterMonitor,
            UIBridge::BoolFn isPal)
                : ui_(ui),
                  media_(media),
                  input_(input),
                  uiPaused_(uiPaused),
                  running_(running),
                  warmReset_(std::move(warmReset)),
                  coldReset_(std::move(coldReset)),
                  setVideoMode_(std::move(setVideoMode)),
                  enterMonitor_(std::move(enterMonitor)),
                  isPal_(std::move(isPal))
{

}

UIBridge::~UIBridge() = default;

EmulatorUI::MediaViewState UIBridge::buildMediaViewState() const
{
    EmulatorUI::MediaViewState s;

    if (media_)
    {
        const auto& m = media_->getState();
        s.diskAttached = m.diskAttached;
        s.diskPath     = m.diskPath;
        s.cartAttached = m.cartAttached;
        s.cartPath     = m.cartPath;
        s.tapeAttached = m.tapeAttached;
        s.tapePath     = m.tapePath;
        s.prgAttached  = m.prgAttached;
        s.prgPath      = m.prgPath;
    }

    if (input_)
    {
        s.joy1Attached = input_->isJoy1Attached();
        s.joy2Attached = input_->isJoy2Attached();

        auto p1 = input_->getPad1();
        auto p2 = input_->getPad2();

        s.pad1Name = p1 ? SDL_GameControllerName(p1) : "None";
        s.pad2Name = p2 ? SDL_GameControllerName(p2) : "None";
    }
    else
    {
        s.joy1Attached = false;
        s.joy2Attached = false;
        s.pad1Name = "None";
        s.pad2Name = "None";
    }

    s.paused = uiPaused_.load();
    s.pal    = isPal_ ? isPal_() : false;

    return s;
}

void UIBridge::processCommands()
{
    for (const auto& cmd : ui_.consumeCommands())
    {
        switch (cmd.type)
        {
            case UiCommand::Type::AttachDisk:
                if (media_)
                {
                    MediaManager::DriveModel model =
                        (cmd.driveType == UiCommand::DriveType::D1571) ? MediaManager::DriveModel::D1571 :
                        (cmd.driveType == UiCommand::DriveType::D1581) ? MediaManager::DriveModel::D1581 :
                                                                         MediaManager::DriveModel::D1541;
                    media_->attachDiskImage(cmd.deviceNum, model, cmd.path);
                }
                break;

            case UiCommand::Type::AttachPRG:
                if (media_)
                {
                    media_->setPrgPath(cmd.path);
                    media_->attachPRGImage();
                }
                break;

            case UiCommand::Type::AttachCRT:
                if (media_)
                {
                    media_->setCartPath(cmd.path);
                    media_->attachCRTImage();
                }
                break;

            case UiCommand::Type::AttachT64:
                if (media_)
                {
                    media_->setTapePath(cmd.path);
                    media_->attachT64Image();
                }
                break;

            case UiCommand::Type::AttachTAP:
                if (media_)
                {
                    media_->setTapePath(cmd.path);
                    media_->attachTAPImage();
                }
                break;

            case UiCommand::Type::WarmReset:
                if (warmReset_) warmReset_();
                break;

            case UiCommand::Type::ColdReset:
                if (coldReset_) coldReset_();
                break;

            case UiCommand::Type::SetPAL:
                if (setVideoMode_) setVideoMode_("PAL");
                break;

            case UiCommand::Type::SetNTSC:
                if (setVideoMode_) setVideoMode_("NTSC");
                break;

            case UiCommand::Type::TogglePause:
                uiPaused_ = !uiPaused_.load();
                break;

            case UiCommand::Type::ToggleJoy1:
                if (input_) input_->setJoystickAttached(1, !input_->isJoy1Attached());
                break;

            case UiCommand::Type::ToggleJoy2:
                if (input_) input_->setJoystickAttached(2, !input_->isJoy2Attached());
                break;

            case UiCommand::Type::AssignPad1ToPort1:
                if (input_ && input_->getPad1()) input_->assignPadToPort(input_->getPad1(), 1);
                break;

            case UiCommand::Type::AssignPad1ToPort2:
                if (input_ && input_->getPad1()) input_->assignPadToPort(input_->getPad1(), 2);
                break;

            case UiCommand::Type::AssignPad2ToPort1:
                if (input_ && input_->getPad2()) input_->assignPadToPort(input_->getPad2(), 1);
                break;

            case UiCommand::Type::AssignPad2ToPort2:
                if (input_ && input_->getPad2()) input_->assignPadToPort(input_->getPad2(), 2);
                break;

            case UiCommand::Type::ClearPort1Pad:
                if (input_) input_->clearPortPad(1);
                break;

            case UiCommand::Type::ClearPort2Pad:
                if (input_) input_->clearPortPad(2);
                break;

            case UiCommand::Type::SwapPortPads:
                if (input_) input_->swapPortPads();
                break;

            case UiCommand::Type::CassPlay:
                if (media_) media_->tapePlay();
                break;

            case UiCommand::Type::CassStop:
                if (media_) media_->tapeStop();
                break;

            case UiCommand::Type::CassRewind:
                if (media_) media_->tapeRewind();
                break;

            case UiCommand::Type::CassEject:
                if (media_) media_->tapeEject();
                break;

            case UiCommand::Type::EnterMonitor:
                if (enterMonitor_) enterMonitor_();
                break;

            case UiCommand::Type::Quit:
                running_ = false;
                break;

            default:
                break;
        }
    }
}
