// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef IHASBUTTON_H_INCLUDED
#define IHASBUTTON_H_INCLUDED

struct IHasButton
{
    virtual ~IHasButton() = default;

    virtual uint32_t getButtonCount() const = 0;
    virtual const char* getButtonName(uint32_t buttonIndex) const = 0;
    virtual void pressButton(uint32_t index) = 0;
};


#endif // IHASBUTTON_H_INCLUDED
