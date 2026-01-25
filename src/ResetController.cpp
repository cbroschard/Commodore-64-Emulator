#include "ResetController.h"

#include "Cartridge.h"
#include "CIA1.h"
#include "CIA2.h"
#include "CPU.h"
#include "IECBUS.h"
#include "MediaManager.h"
#include "Memory.h"
#include "PLA.h"
#include "SID/SID.h"
#include "Vic.h"

ResetController::ResetController(
    CPU& cpu,
    Memory& mem,
    PLA& pla,
    CIA1& cia1,
    CIA2& cia2,
    Vic& vic,
    SID& sid,
    IECBUS& bus,
    Cartridge& cart,
    MediaManager* media,
    const std::string& basicRom,
    const std::string& kernalRom,
    const std::string& charRom,
    VideoMode& videoMode,
    const CPUConfig*& cpuCfg)
    : cpu_(cpu)
    , mem_(mem)
    , pla_(pla)
    , cia1_(cia1)
    , cia2_(cia2)
    , vic_(vic)
    , sid_(sid)
    , bus_(bus)
    , cart_(cart)
    , media_(media)
    , basicRom_(basicRom)
    , kernalRom_(kernalRom)
    , charRom_(charRom)
    , videoMode_(videoMode)
    , cpuCfg_(cpuCfg)
{
}

void ResetController::setVideoMode(const std::string& mode)
{
    if (mode == "PAL" || mode == "pal")
    {
        videoMode_ = VideoMode::PAL;
        cpuCfg_ = &PAL_CPU;
    }
    else
    {
        videoMode_ = VideoMode::NTSC;
        cpuCfg_ = &NTSC_CPU;
    }

    cpu_.setMode(videoMode_);
    vic_.setMode(videoMode_);
    sid_.setMode(videoMode_);
    cia1_.setMode(videoMode_);
    cia2_.setMode(videoMode_);
    if (media_) media_->setVideoMode(videoMode_);
}

void ResetController::warmReset()
{
    const bool cartAttachedNow = (media_ && media_->getState().cartAttached);
    if (!cartAttachedNow)
    {
        cart_.setGameLine(true);
        cart_.setExROMLine(true);
    }

    pla_.updateMemoryControlRegister(0x37);

    bus_.reset();
    vic_.reset();
    cia1_.reset();
    cia2_.reset();
    sid_.reset();

    cpu_.reset();
}

void ResetController::coldReset()
{
    const bool cartAttachedNow = (media_ && media_->getState().cartAttached);
    if (!cartAttachedNow)
    {
        cart_.setGameLine(true);
        cart_.setExROMLine(true);
    }

    if (!mem_.Initialize(basicRom_, kernalRom_, charRom_))
        throw std::runtime_error("Error: Problem encountered initializing memory!");

    pla_.reset();

    if (cartAttachedNow)
    {
        mem_.setCartridgeAttached(true);
        pla_.setCartridgeAttached(true);
    }
    else
    {
        mem_.setCartridgeAttached(false);
        pla_.setCartridgeAttached(false);
    }

    bus_.reset();
    vic_.reset();
    cia1_.reset();
    cia2_.reset();
    sid_.reset();

    cpu_.reset();
}
