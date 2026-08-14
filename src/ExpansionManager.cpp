// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "ExpansionManager.h"

ExpansionManager::ExpansionManager(Computer& host) :
    host_(host)
{

}

ExpansionManager::~ExpansionManager() = default;

void ExpansionManager::attachVirtualModem()
{
    host_.attachVirtualModem();
}

void ExpansionManager::detachVirtualModem()
{
    host_.detachVirtualModem();
}

bool ExpansionManager::isVirtualModemAttached() const
{
    return host_.isVirtualModemAttached();
}

bool ExpansionManager::isVirtualModemOnline() const
{
    return host_.isVirtualModemOnline();
}

void ExpansionManager::setRS232Baud(uint32_t baud)
{
    host_.setRS232Baud(baud);
}

uint32_t ExpansionManager::getRS232Baud() const
{
    return host_.getRS232Baud();
}

void ExpansionManager::enableSwiftLink(uint16_t baseAddress)
{
    host_.enableSwiftLink(baseAddress);
}

void ExpansionManager::disableSwiftLink()
{
    host_.disableSwiftLink();
}

void ExpansionManager::enableSwiftLink()
{
    host_.enableSwiftLink();
}

bool ExpansionManager::isSwiftLinkEnabled() const
{
    return host_.isSwiftLinkEnabled();
}

void ExpansionManager::setSwiftLinkBaseAddress(uint16_t baseAddress)
{
    host_.setSwiftLinkBaseAddress(baseAddress);
}

uint16_t ExpansionManager::getSwiftLinkBaseAddress() const
{
    return host_.getSwiftLinkBaseAddress();
}

void ExpansionManager::attachSwiftLinkVirtualModem()
{
    host_.attachSwiftLinkVirtualModem();
}

void ExpansionManager::detachSwiftLinkVirtualModem()
{
    host_.detachSwiftLinkVirtualModem();
}

bool ExpansionManager::isSwiftLinkVirtualModemAttached() const
{
   return host_.isSwiftLinkVirtualModemAttached();
}

bool ExpansionManager::isSwiftLinkVirtualModemOnline() const
{
    return host_.isSwiftLinkVirtualModemOnline();
}

void ExpansionManager::enableTurbo232(uint16_t baseAddress)
{
    host_.enableTurbo232(baseAddress);
}

void ExpansionManager::disableTurbo232()
{
    host_.disableTurbo232();
}

void ExpansionManager::enableTurbo232()
{
    host_.enableTurbo232();
}

bool ExpansionManager::isTurbo232Enabled() const
{
    return host_.isTurbo232Enabled();
}

void ExpansionManager::setTurbo232BaseAddress(uint16_t baseAddress)
{
    host_.setTurbo232BaseAddress(baseAddress);
}

uint16_t ExpansionManager::getTurbo232BaseAddress() const
{
    return host_.getTurbo232BaseAddress();
}

void ExpansionManager::attachTurbo232VirtualModem()
{
    host_.attachTurbo232VirtualModem();
}

void ExpansionManager::detachTurbo232VirtualModem()
{
    host_.detachTurbo232VirtualModem();
}

bool ExpansionManager::isTurbo232VirtualModemAttached() const
{
   return host_.isTurbo232VirtualModemAttached();
}

bool ExpansionManager::isTurbo232VirtualModemOnline() const
{
    return host_.isTurbo232VirtualModemOnline();
}
