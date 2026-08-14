#include <SDL3/SDL.h>
#include "Cartridge.h"
#include "Cartridge/CartridgeMapper.h"
#include "Cartridge/IHasButton.h"
#include "Cartridge/IHasIDE64Storage.h"
#include "Cartridge/IHasSwitch.h"
#include "InputManager.h"
#include "MediaManager.h"
#include "UIBridge.h"

UIBridge::UIBridge(EmulatorUI& ui,
                   ExpansionManager& expansionManager,
                   MediaManager* media,
                   InputManager* input,
                   std::atomic<bool>& uiPaused,
                   std::atomic<bool>& running,
                   UIBridge::StringFn saveState,
                   UIBridge::StringFn loadState,
                   UIBridge::VoidFn warmReset,
                   UIBridge::VoidFn coldReset,
                   UIBridge::StringFn setSIDModel,
                   UIBridge::StringFn setVideoMode,
                   UIBridge::VoidFn enterMonitor,
                   UIBridge::BoolFn isPal,
                   UIBridge::BoolFn is8580,
                   UIBridge::BoolFn isMonitorOpen)
    : ui_(ui),
      expansionManager_(expansionManager),
      media_(media),
      input_(input),
      uiPaused_(uiPaused),
      running_(running),
      saveState_(std::move(saveState)),
      loadState_(std::move(loadState)),
      warmReset_(std::move(warmReset)),
      coldReset_(std::move(coldReset)),
      setSIDModel_(std::move(setSIDModel)),
      setVideoMode_(std::move(setVideoMode)),
      enterMonitor_(std::move(enterMonitor)),
      isPal_(std::move(isPal)),
      is8580_(std::move(is8580)),
      manualPaused_(false),
      dialogPaused_(false),
      isMonitorOpen_(std::move(isMonitorOpen))
{

}

UIBridge::~UIBridge() = default;

void UIBridge::refreshPauseState()
{
    dialogPaused_ = ui_.isFileDialogOpen();
    const bool monitorOpen = isMonitorOpen_ ? isMonitorOpen_() : false;
    uiPaused_ = (manualPaused_ || dialogPaused_ || monitorOpen);
}

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

        s.reuEnabled = m.reuEnabled;
        s.reuSizeKB  = static_cast<uint32_t>(bytesForREUModel(m.reuModel) / 1024);

        media_->fillDriveStatusViews(s.drives);
    }

    if (input_)
    {
        s.joy1Attached = input_->isJoy1Attached();
        s.joy2Attached = input_->isJoy2Attached();

        auto* p1 = input_->getPad1();
        auto* p2 = input_->getPad2();

        const char* pad1Name = p1 ? SDL_GetGamepadName(p1) : nullptr;
        const char* pad2Name = p2 ? SDL_GetGamepadName(p2) : nullptr;

        s.pad1Name = pad1Name ? pad1Name : "None";
        s.pad2Name = pad2Name ? pad2Name : "None";
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
    s.sid8580 = is8580_ ? is8580_() : false;

    s.virtualModemAttached = expansionManager_.isVirtualModemAttached();
    s.virtualModemOnline = expansionManager_.isVirtualModemOnline();
    s.rs232Baud = expansionManager_.getRS232Baud();

    s.swiftLinkEnabled = expansionManager_.isSwiftLinkEnabled();
    s.swiftLinkBaseAddress = expansionManager_.getSwiftLinkBaseAddress();
    s.swiftLinkVirtualModemAttached = expansionManager_.isSwiftLinkVirtualModemAttached();
    s.swiftLinkVirtualModemOnline = expansionManager_.isSwiftLinkVirtualModemOnline();

    s.turbo232Enabled = expansionManager_.isTurbo232Enabled();
    s.turbo232BaseAddress = expansionManager_.getTurbo232BaseAddress();
    s.turbo232VirtualModemAttached = expansionManager_.isTurbo232VirtualModemAttached();
    s.turbo232VirtualModemOnline = expansionManager_.isTurbo232VirtualModemOnline();

    s.cartSwitches.clear();
    s.cartButtons.clear();

    s.ide64Available = false;
    s.ide64Devices.clear();

    if (media_ && s.cartAttached)
    {
        if (auto* cart = media_->getCartridge())
        {
            const CartridgeMapper* mapper = cart->getMapper();

            // Switch capability
            if (auto* hs = dynamic_cast<const IHasSwitch*>(mapper))
            {
                const uint32_t sc = hs->getSwitchCount();
                s.cartSwitches.reserve(sc);

                for (uint32_t si = 0; si < sc; ++si)
                {
                    EmulatorUI::CartSwitchView sw{};
                    sw.name = hs->getSwitchName(si);

                    const uint32_t pc = hs->getSwitchPositionCount(si);
                    sw.positions.reserve(pc);

                    for (uint32_t p = 0; p < pc; ++p)
                        sw.positions.emplace_back(hs->getSwitchPositionLabel(si, p));

                    sw.currentPos = hs->getSwitchPosition(si);

                    s.cartSwitches.push_back(std::move(sw));
                }
            }
            if (auto* hb = dynamic_cast<const IHasButton*>(mapper))
            {
                const uint32_t bc = hb->getButtonCount();
                s.cartButtons.reserve(bc);

                for (uint32_t bi = 0; bi < bc; ++bi)
                {
                    EmulatorUI::CartButtonView bu{};
                    bu.index = bi;
                    bu.name = hb->getButtonName(bi);
                    bu.enabled = true;

                    s.cartButtons.push_back(std::move(bu));
                }
            }
            if (auto* ide64 = dynamic_cast<const IHasIDE64Storage*>(mapper))
            {
                s.ide64Available = true;

                const uint32_t count = ide64->getIDE64DeviceCount();
                s.ide64Devices.reserve(count);

                for (uint32_t index = 0; index < count; ++index)
                {
                    EmulatorUI::IDE64DeviceView dev{};

                    dev.index = index;
                    dev.name = ide64->getIDE64DeviceName(index);
                    dev.present = ide64->isIDE64DevicePresent(index);
                    dev.readOnly = ide64->isIDE64DeviceReadOnly(index);
                    dev.dirty = ide64->isIDE64DeviceDirty(index);
                    dev.sectors = ide64->getIDE64DeviceSectorCount(index);

                    s.ide64Devices.push_back(std::move(dev));
                }
            }
        }
    }

    return s;
}

void UIBridge::processCommands()
{
    refreshPauseState();

    for (const auto& cmd : ui_.consumeCommands())
    {
        switch (cmd.type)
        {
            case UiCommand::Type::AttachDisk:
                if (media_)
                {
                    DriveModel model =
                        (cmd.driveType == UiCommand::DriveType::D1571) ? DriveModel::D1571 :
                        (cmd.driveType == UiCommand::DriveType::D1581) ? DriveModel::D1581 :
                                                                         DriveModel::D1541;
                    media_->attachDiskImage(cmd.deviceNum, model, cmd.path);
                }
                break;

            case UiCommand::Type::AttachPRG:
                if (media_)
                {
                    media_->setPrgPath(cmd.path);
                    media_->attachPRGImage(MediaManager::PRGLoadMode::Standalone);
                }
                break;

            case UiCommand::Type::AttachPRGWithCartridge:
                if (media_)
                {
                    media_->setPrgPath(cmd.path);
                    media_->attachPRGImage(MediaManager::PRGLoadMode::KeepCartridge);
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

            case UiCommand::Type::AttachVirtualModem:
            {
                expansionManager_.attachVirtualModem();
                break;
            }

            case UiCommand::Type::DetachVirtualModem:
            {
                expansionManager_.detachVirtualModem();
                break;
            }

            case UiCommand::Type::SetRS232Baud:
            {
                expansionManager_.setRS232Baud(cmd.rs232Baud);
                break;
            }

            case UiCommand::Type::EnableSwiftLink:
                    expansionManager_.enableSwiftLink();
                break;

            case UiCommand::Type::DisableSwiftLink:
                    expansionManager_.disableSwiftLink();
                break;

            case UiCommand::Type::SetSwiftLinkBaseAddress:
                    expansionManager_.setSwiftLinkBaseAddress(cmd.swiftLinkBaseAddress);
                break;

            case UiCommand::Type::AttachSwiftLinkVirtualModem:
                expansionManager_.attachSwiftLinkVirtualModem();
                break;

            case UiCommand::Type::DetachSwiftLinkVirtualModem:
                    expansionManager_.detachSwiftLinkVirtualModem();
                break;

            case UiCommand::Type::EnableTurbo232:
                    expansionManager_.enableTurbo232();
                break;

            case UiCommand::Type::DisableTurbo232:
                    expansionManager_.disableTurbo232();
                break;

            case UiCommand::Type::SetTurbo232BaseAddress:
                    expansionManager_.setTurbo232BaseAddress(cmd.turbo232BaseAddress);
                break;

            case UiCommand::Type::AttachTurbo232VirtualModem:
                    expansionManager_.attachTurbo232VirtualModem();
                break;

            case UiCommand::Type::DetachTurbo232VirtualModem:
                    expansionManager_.detachTurbo232VirtualModem();
                break;

            case UiCommand::Type::CreateBlankDisk:
                if (media_)
                {
                    DriveModel model =
                        (cmd.driveType == UiCommand::DriveType::D1571) ? DriveModel::D1571 :
                        (cmd.driveType == UiCommand::DriveType::D1581) ? DriveModel::D1581 :
                                                                         DriveModel::D1541;
                    media_->createBlankDisk(cmd.deviceNum, model, cmd.path);
                }
                break;

            case UiCommand::Type::EjectCRT:
                if (media_)
                    media_->detachCRTImage();
                break;

            case UiCommand::Type::EjectDisk:
                if (media_)
                    media_->detachDiskImage(cmd.deviceNum);
                break;

            case UiCommand::Type::SaveState:
                {
                    uiPaused_ = true;

                    if (saveState_) saveState_(cmd.path);

                    uiPaused_ = false;
                    break;
                }
            case UiCommand::Type::LoadState:
                {
                    uiPaused_ = true;

                    if (loadState_) loadState_(cmd.path);

                    uiPaused_ = false;
                    break;
                }
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
                manualPaused_ = !manualPaused_;
                refreshPauseState();
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

            case UiCommand::Type::SetCartSwitch:
                if (media_) media_->setCartSwitch(cmd.switchIndex, cmd.switchPos);
                break;

            case UiCommand::Type::LoadIDE64Image:
                if (media_)
                {
                     const bool ok = media_->loadIDE64Image(cmd.ide64DeviceIndex, cmd.path, cmd.ide64ReadOnly);
                     (void)ok;
                }
                break;

            case UiCommand::Type::CreateIDE64Image:
                if (media_)
                {
                    const bool ok = media_->createIDE64Image(cmd.ide64DeviceIndex, cmd.path, cmd.ide64Sectors);
                    (void)ok;
                }

                break;

            case UiCommand::Type::SaveIDE64Image:
                if (media_)
                {
                    const bool ok = media_->saveIDE64Image(cmd.ide64DeviceIndex);
                    (void)ok;
                }

                break;

            case UiCommand::Type::EjectIDE64Image:
                if (media_)
                {
                    const bool ok = media_->ejectIDE64Image(cmd.ide64DeviceIndex);
                    (void)ok;
                }

                break;

            case UiCommand::Type::PressButton:
                if (media_) media_->pressButton(cmd.buttonIndex);
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

            case UiCommand::Type::SetMOS6581:
                if (setSIDModel_)
                    setSIDModel_("6581");
                break;

            case UiCommand::Type::SetMOS8580:
                if (setSIDModel_)
                    setSIDModel_("8580");
                break;

            case UiCommand::Type::SetREU:
            {
                if (media_)
                {
                    if (cmd.reuModel == REUModel::None)
                        media_->detachREU();
                    else
                        media_->attachREU(cmd.reuModel);
                }
                break;
            }

            case UiCommand::Type::EnterMonitor:
                if (enterMonitor_) enterMonitor_();
                break;

            case UiCommand::Type::Quit:
            {
                if (media_) media_->flushAndSaveMedia();
                running_ = false;
                break;
            }

            default:
                break;
        }
    }

    refreshPauseState();
}

void UIBridge::toggleManualPause()
{
    manualPaused_ = !manualPaused_;
    refreshPauseState();
}

void UIBridge::setManualPause(bool paused)
{
    manualPaused_ = paused;
    refreshPauseState();
}

