// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef MIXER_H
#define MIXER_H

#include <algorithm>
#include <cmath>
#include <vector>

class Mixer
{
    public:
        Mixer();
        virtual ~Mixer();

        double mixSamples(const std::vector<double>& voiceSamples);

    protected:

    private:
};

#endif // MIXER_H
