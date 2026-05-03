// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef SID_ENVELOPE_TABLES_H
#define SID_ENVELOPE_TABLES_H

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

#endif // SID_ENVELOPE_TABLES_H
