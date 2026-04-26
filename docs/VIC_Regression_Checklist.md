# VIC-II Regression Checklist

This checkpoint represents the current cycle-timed scanline VIC implementation before moving toward a more hardware-accurate pixel pipeline.

## Current architecture notes

- `Vic::tick()` is cycle-oriented.
- Cycle decision points currently handle raster IRQ sampling, badline sampling, sprite DMA start, DMA latch timing, and cycle 58 display carry decisions.
- Bus arbitration currently runs once per VIC cycle.
- Fetches currently run once per VIC cycle through the existing fetch phase.
- Final visible output is still mostly scanline-based through `finalizeCurrentRasterLine()`.

## Known limitation

Rendering, sprite output, and collision detection are still finalized at the end of the raster line instead of being emitted live per dot/pixel. This means mid-line effects are still approximate.

## Test ROMs / Games

### Pac-Man / Ms. Pac-Man
- Music tempo should not slow down.
- Ghost count should remain correct.
- Sprites should not corrupt.
- Border position should not regress.

### BC's Quest for Tires
- Main sprite should remain visible.
- Bottom border should remain stable.
- Multicolor text alignment should not regress.

### Q*bert
- Selecting 1 or 2 players should not hang.
- Watch for DEN / D011 polling behavior.
- Screen should not stay blank unexpectedly.

### Frog Master
- Watch for flashing or jittery lines near the bottom.
- Watch sprite timing near lower screen area.

### RoboCop 2 / RoboCop 3
- Watch for stalls in raster IRQ-heavy sections.

### Super Zaxxon
- Screen should not blank or become malformed.
- Watch raster IRQ behavior.

### Spy Hunter
- Watch truck sprite synchronization.

## Before starting Phase II

Confirm:
- Project builds cleanly.
- BASIC screen still renders.
- At least Pac-Man or Ms. Pac-Man still runs at normal music speed.
- No new VIC warnings or trace spam.