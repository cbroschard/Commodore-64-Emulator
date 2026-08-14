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
                   MediaManager* media,
                   InputManager* input,
                   std::atomic<bool>& uiPaused,
                   std::atomic<bool>& running,
                   UIBridge::VoidFn attachVirtualModem,
                   UIBridge::VoidFn detachVirtualModem,
                   UIBridge::BoolFn isVirtualModemAttached,
                   UIBridge::BoolFn isVirtualModemOnline,
                   UIBridge::SetUInt32Fn setRS232Baud,
                   UIBridge::UInt32Fn getRS232Baud,
                   UIBridge::VoidFn enableSwiftLink,
                   UIBridge::VoidFn disableSwiftLink,
                   UIBridge::BoolFn isSwiftLinkEnabled,
                   UIBridge::SetUInt32Fn setSwiftLinkBaseAddress,
                   UIBridge::UInt32Fn getSwiftLinkBaseAddress,
                   UIBridge::VoidFn attachSwiftLinkVirtualModem,
                   UIBridge::VoidFn detachSwiftLinkVirtualModem,
                   UIBridge::BoolFn isSwiftLinkVirtualModemAttached,
                   UIBridge::BoolFn isSwiftLinkVirtualModemOnline,
                   UIBridge::VoidFn enableTurbo232,
                   UIBridge::VoidFn disableTurbo232,
                   UIBridge::BoolFn isTurbo232Enabled,
                   UIBridge::SetUInt32Fn setTurbo232BaseAddress,
                   UIBridge::UInt32Fn getTurbo232BaseAddress,
                   UIBridge::VoidFn attachTurbo232VirtualModem,
                   UIBridge::VoidFn detachTurbo232VirtualModem,
                   UIBridge::BoolFn isTurbo232VirtualModemAttached,
                   UIBridge::BoolFn isTurbo232VirtualModemOnline,UIBridge::StringFn saveState,
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
      media_(media),
      input_(input),
      uiPaused_(uiPaused),
      running_(running),
      attachVirtualModem_(attachVirtualModem),
      detachVirtualModem_(detachVirtualModem),
      isVirtualModemAttached_(std::move(isVirtualModemAttached)),
      isVirtualModemOnline_(std::move(isVirtualModemOnline)),
      setRS232Baud_(std::move(setRS232Baud)),
      getRS232Baud_(std::move(getRS232Baud)),
      enableSwiftLink_(std::move(enableSwiftLink)),
      disableSwiftLink_(std::move(disableSwiftLink)),
      isSwiftLinkEnabled_(std::move(isSwiftLinkEnabled)),
      setSwiftLinkBaseAddress_(std::move(setSwiftLinkBaseAddress)),
      getSwiftLinkBaseAddress_(std::move(getSwiftLinkBaseAddress)),
      attachSwiftLinkVirtualModem_(std::move(attachSwiftLinkVirtualModem)),
      detachSwiftLinkVirtualModem_(std::move(detachSwiftLinkVirtualModem)),
      isSwiftLinkVirtualModemAttached_(std::move(isSwiftLinkVirtualModemAttached)),
      isSwiftLinkVirtualModemOnline_(std::move(isSwiftLinkVirtualModemOnline)),
      enableTurbo232_(std::move(enableTurbo232)),
      disableTurbo232_(std::move(disableTurbo232)),
      isTurbo232Enabled_(std::move(isTurbo232Enabled)),
      setTurbo232BaseAddress_(std::move(setTurbo232BaseAddress)),
      getTurbo232BaseAddress_(std::move(getTurbo232BaseAddress)),
      attachTurbo232VirtualModem_(std::move(attachTurbo232VirtualModem)),
      detachTurbo232VirtualModem_(std::move(detachTurbo232VirtualModem)),
      isTurbo232VirtualModemAttached_(std::move(isTurbo232VirtualModemAttached)),
      isTurbo232VirtualModemOnline_(std::move(isTurbo232VirtualModemOnline)),
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

    s.virtualModemAttached = isVirtualModemAttached_ ? isVirtualModemAttached_() : false;
    s.virtualModemOnline = isVirtualModemOnline_ ? isVirtualModemOnline_() : false;
    s.rs232Baud = getRS232Baud_ ? getRS232Baud_() : 300;

    s.swiftLinkEnabled = isSwiftLinkEnabled_ ? isSwiftLinkEnabled_() : false;
    s.swiftLinkBaseAddress = static_cast<uint16_t>(getSwiftLinkBaseAddress_ ? getSwiftLinkBaseAddress_() : 0xDE00);
    s.swiftLinkVirtualModemAttached = isSwiftLinkVirtualModemAttached_ ? isSwiftLinkVirtualModemAttached_() : false;
    s.swiftLinkVirtualModemOnline = isSwiftLinkVirtualModemOnline_ ? isSwiftLinkVirtualModemOnline_() : false;

    s.turbo232Enabled = isTurbo232Enabled_ ? isTurbo232Enabled_() : false;
    s.turbo232BaseAddress = static_cast<uint16_t>(getTurbo232BaseAddress_ ? getTurbo232BaseAddress_() : 0xDE00);
    s.turbo232VirtualModemAttached = isTurbo232VirtualModemAttached_ ? isTurbo232VirtualModemAttached_() : false;
    s.turbo232VirtualModemOnline = isTurbo232VirtualModemOnline_ ? isTurbo232VirtualModemOnline_() : false;

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
                if (attachVirtualModem_)
                    attachVirtualModem_();
                break;
            }

            case UiCommand::Type::DetachVirtualModem:
            {
                if (detachVirtualModem_)
                    detachVirtualModem_();
                break;
            }

            case UiCommand::Type::SetRS232Baud:
            {
                if (setRS232Baud_)
                    setRS232Baud_(cmd.rs232Baud);

                break;
            }

            case UiCommand::Type::EnableSwiftLink:
                if (enableSwiftLink_)
                    enableSwiftLink_();
                break;

            case UiCommand::Type::DisableSwiftLink:
                if (disableSwiftLink_)
                    disableSwiftLink_();
                break;

            case UiCommand::Type::SetSwiftLinkBaseAddress:
                if (setSwiftLinkBaseAddress_)
                    setSwiftLinkBaseAddress_(cmd.swiftLinkBaseAddress);
                break;

            case UiCommand::Type::AttachSwiftLinkVirtualModem:
                if (attachSwiftLinkVirtualModem_)
                    attachSwiftLinkVirtualModem_();
                break;

            case UiCommand::Type::DetachSwiftLinkVirtualModem:
                if (detachSwiftLinkVirtualModem_)
                    detachSwiftLinkVirtualModem_();
                break;

            case UiCommand::Type::EnableTurbo232:
                if (enableTurbo232_)
                    enableTurbo232_();
                break;

            case UiCommand::Type::DisableTurbo232:
                if (disableTurbo232_)
                    disableTurbo232_();
                break;

            case UiCommand::Type::SetTurbo232BaseAddress:
                if (setTurbo232BaseAddress_)
                    setTurbo232BaseAddress_(cmd.turbo232BaseAddress);
                break;

            case UiCommand::Type::AttachTurbo232VirtualModem:
                if (attachTurbo232VirtualModem_)
                    attachTurbo232VirtualModem_();
                break;

            case UiCommand::Type::DetachTurbo232VirtualModem:
                if (detachTurbo232VirtualModem_)
                    detachTurbo232VirtualModem_();
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

