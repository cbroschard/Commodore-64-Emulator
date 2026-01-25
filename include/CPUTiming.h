#ifndef CPUTIMING_H_INCLUDED
#define CPUTIMING_H_INCLUDED

struct CPUConfig
{
    double clockSpeedHz;   // master clock
    double frameRate;      // frames/sec

    // compute on-the-fly so it can never get out of sync
    constexpr int cyclesPerFrame() const
    {
        return int(clockSpeedHz / frameRate + 0.5);
    }
};

inline constexpr CPUConfig NTSC_CPU{1'022'727.0, 59.826};
inline constexpr CPUConfig PAL_CPU {  985'248.0, 50.125};

#endif // CPUTIMING_H_INCLUDED
