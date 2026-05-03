// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef SID_ENVELOPE_TABLES_H
#define SID_ENVELOPE_TABLES_H

static constexpr double SID_CLOCK_NTSC = 1022727.0;
static constexpr double SID_CLOCK_PAL  = 985248.0;

// Approximate ADSR total times in seconds.
// Kept for debug display / compatibility with setParameters().
static constexpr double SID_ATTACK_S[16] =
{
    0.002, 0.008, 0.016, 0.024,
    0.038, 0.056, 0.068, 0.080,
    0.100, 0.250, 0.500, 0.800,
    1.000, 3.000, 5.000, 8.000
};

static constexpr double SID_DECAY_RELEASE_S[16] =
{
    0.006, 0.024, 0.048, 0.072,
    0.114, 0.168, 0.204, 0.240,
    0.300, 0.750, 1.500, 2.400,
    3.000, 9.000, 15.000, 24.000
};

// Per-envelope-step periods in NTSC SID cycles.
// Envelope::setADSR() should scale these by:
//     sidClockFrequency / SID_CLOCK_NTSC
// so PAL keeps approximately the same real-time ADSR durations.
static constexpr double SID_ATTACK_STEP_CYCLES_NTSC[16] =
{
    8.021, 32.086, 64.171, 96.257,
    152.593, 224.599, 272.727, 320.856,
    401.069, 1002.673, 2005.347, 3208.555,
    4010.694, 12032.082, 20053.471, 32085.553
};

static constexpr double SID_DECAY_RELEASE_STEP_CYCLES_NTSC[16] =
{
    24.064, 96.257, 192.514, 288.770,
    457.455, 673.561, 818.182, 962.567,
    1203.208, 3008.020, 6016.041, 9625.694,
    12032.082, 36096.247, 60160.412, 96256.659
};

#endif // SID_ENVELOPE_TABLES_H
