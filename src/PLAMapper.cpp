// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "PLAMapper.h"

PLAMapper::PLAMapper() = default;

PLAMapper::~PLAMapper() = default;

static const PLAMapper::modeMapping mappings[32] =
{
    // Mode 0: (exROM=0, game=0, charen=0, hiram=0, loram=0)
    {
        {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::RAM, 0},
            {0x8000, 0x9FFF, PLA::CARTRIDGE_LO, 0x8000},
            {0xA000, 0xBFFF, PLA::CARTRIDGE_HI, 0xA000},
            {0xC000, 0xCFFF, PLA::RAM, 0},
            {0xD000, 0xDFFF, PLA::RAM, 0},
            {0xE000, 0xFFFF, PLA::RAM, 0}
        }
    },
    // Mode 1: (exROM=0, game=0, charen=0, hiram=0, loram=1)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::RAM, 0},
            {0x8000, 0x9FFF, PLA::CARTRIDGE_LO, 0x8000},
            {0xA000, 0xBFFF, PLA::CARTRIDGE_HI, 0xA000},
            {0xC000, 0xCFFF, PLA::RAM, 0},
            {0xD000, 0xDFFF, PLA::RAM, 0},
            {0xE000, 0xFFFF, PLA::RAM, 0}
       }
    },
    // Mode 2: (exROM=0, game=0, charen=0, hiram=1, loram=0)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::RAM, 0},
            {0x8000, 0x9FFF, PLA::CARTRIDGE_LO, 0x8000},
            {0xA000, 0xBFFF, PLA::CARTRIDGE_HI, 0xA000},
            {0xC000, 0xCFFF, PLA::RAM, 0},
            {0xD000, 0xDFFF, PLA::CHARACTER_ROM, 0xD000},
            {0xE000, 0xFFFF, PLA::KERNAL_ROM, 0xE000}
       }
    },
    // Mode 3: (exROM=0, game=0, charen=0, hiram=1, loram=1)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::RAM, 0},
            {0x8000, 0x9FFF, PLA::CARTRIDGE_LO, 0x8000},
            {0xA000, 0xBFFF, PLA::CARTRIDGE_HI, 0xA000},
            {0xC000, 0xCFFF, PLA::RAM, 0},
            {0xD000, 0xDFFF, PLA::CHARACTER_ROM, 0xD000},
            {0xE000, 0xFFFF, PLA::KERNAL_ROM, 0xE000}
       }
    },
    // Mode 4: (exROM=0, game=0, charen=1, hiram=0, loram=0)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::RAM, 0},
            {0x8000, 0x9FFF, PLA::CARTRIDGE_LO, 0x8000},
            {0xA000, 0xBFFF, PLA::CARTRIDGE_HI, 0xA000},
            {0xC000, 0xCFFF, PLA::RAM, 0},
            {0xD000, 0xDFFF, PLA::IO, 0},
            {0xE000, 0xFFFF, PLA::RAM, 0}
       }
    },
    // Mode 5: (exROM=0, game=0, charen=1, hiram=0, loram=1)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::RAM, 0},
            {0x8000, 0x9FFF, PLA::CARTRIDGE_LO, 0x8000},
            {0xA000, 0xBFFF, PLA::CARTRIDGE_HI, 0xA000},
            {0xC000, 0xCFFF, PLA::RAM, 0},
            {0xD000, 0xDFFF, PLA::IO, 0},
            {0xE000, 0xFFFF, PLA::RAM, 0}
        }
    },
    // Mode 6: (exROM=0, game=0, charen=1, hiram=1, loram=0)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::RAM, 0},
            {0x8000, 0x9FFF, PLA::CARTRIDGE_LO, 0x8000},
            {0xA000, 0xBFFF, PLA::CARTRIDGE_HI, 0xA000},
            {0xC000, 0xCFFF, PLA::RAM, 0},
            {0xD000, 0xDFFF, PLA::IO, 0},
            {0xE000, 0xFFFF, PLA::KERNAL_ROM, 0xE000}
       }
    },
    // Mode 7: (exROM=0, game=0, charen=1, hiram=1, loram=1)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::RAM, 0},
            {0x8000, 0x9FFF, PLA::CARTRIDGE_LO, 0x8000},
            {0xA000, 0xBFFF, PLA::CARTRIDGE_HI, 0xA000},
            {0xC000, 0xCFFF, PLA::RAM, 0},
            {0xD000, 0xDFFF, PLA::IO, 0},
            {0xE000, 0xFFFF, PLA::KERNAL_ROM, 0xE000}
       }
    },
    // Mode 8: (exROM=0, game=1, charen=0, hiram=0, loram=0)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::RAM, 0},
            {0x8000, 0x9FFF, PLA::CARTRIDGE_LO, 0x8000},
            {0xA000, 0xBFFF, PLA::RAM, 0},
            {0xC000, 0xCFFF, PLA::RAM, 0},
            {0xD000, 0xDFFF, PLA::RAM, 0},
            {0xE000, 0xFFFF, PLA::RAM, 0}
       }
    },
    // Mode 9: (exROM=0, game=1, charen=0, hiram=0, loram=1)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::RAM, 0},
            {0x8000, 0x9FFF, PLA::CARTRIDGE_LO, 0x8000},
            {0xA000, 0xBFFF, PLA::RAM, 0},
            {0xC000, 0xCFFF, PLA::RAM, 0},
            {0xD000, 0xDFFF, PLA::RAM, 0},
            {0xE000, 0xFFFF, PLA::RAM, 0}
       }
    },
    // Mode 10: (exROM=0, game=1, charen=0, hiram=1, loram=0)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::RAM, 0},
            {0x8000, 0x9FFF, PLA::CARTRIDGE_LO, 0x8000},
            {0xA000, 0xBFFF, PLA::RAM, 0},
            {0xC000, 0xCFFF, PLA::RAM, 0},
            {0xD000, 0xDFFF, PLA::CHARACTER_ROM, 0xD000},
            {0xE000, 0xFFFF, PLA::KERNAL_ROM, 0xE000}
       }
    },
     // Mode 11: (exROM=0, game=1, charen=0, hiram=1, loram=1)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::RAM, 0},
            {0x8000, 0x9FFF, PLA::CARTRIDGE_LO, 0x8000},
            {0xA000, 0xBFFF, PLA::RAM, 0},
            {0xC000, 0xCFFF, PLA::RAM, 0},
            {0xD000, 0xDFFF, PLA::CHARACTER_ROM, 0xD000},
            {0xE000, 0xFFFF, PLA::KERNAL_ROM, 0xE000}
       }
    },
     // Mode 12: (exROM=0, game=1, charen=1, hiram=0, loram=0)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::RAM, 0},
            {0x8000, 0x9FFF, PLA::CARTRIDGE_LO, 0x8000},
            {0xA000, 0xBFFF, PLA::RAM, 0},
            {0xC000, 0xCFFF, PLA::RAM, 0},
            {0xD000, 0xDFFF, PLA::IO, 0},
            {0xE000, 0xFFFF, PLA::RAM, 0}
       }
    },
     // Mode 13: (exROM=0, game=1, charen=1, hiram=0, loram=1)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::RAM, 0},
            {0x8000, 0x9FFF, PLA::CARTRIDGE_LO, 0x8000},
            {0xA000, 0xBFFF, PLA::RAM, 0},
            {0xC000, 0xCFFF, PLA::RAM, 0},
            {0xD000, 0xDFFF, PLA::IO, 0},
            {0xE000, 0xFFFF, PLA::RAM, 0}
       }
    },
     // Mode 14: (exROM=0, game=1, charen=1, hiram=1, loram=0)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::RAM, 0},
            {0x8000, 0x9FFF, PLA::CARTRIDGE_LO, 0x8000},
            {0xA000, 0xBFFF, PLA::RAM, 0},
            {0xC000, 0xCFFF, PLA::RAM, 0},
            {0xD000, 0xDFFF, PLA::IO, 0},
            {0xE000, 0xFFFF, PLA::KERNAL_ROM, 0xE000}
       }
    },
     // Mode 15: (exROM=0, game=1, charen=1, hiram=1, loram=1)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::RAM, 0},
            {0x8000, 0x9FFF, PLA::CARTRIDGE_LO, 0x8000},
            {0xA000, 0xBFFF, PLA::RAM, 0},
            {0xC000, 0xCFFF, PLA::RAM, 0},
            {0xD000, 0xDFFF, PLA::IO, 0},
            {0xE000, 0xFFFF, PLA::KERNAL_ROM, 0xE000}
       }
    },
     // Mode 16: (exROM=1, game=0, charen=0, hiram=0, loram=0)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::UNMAPPED, 0},
            {0x8000, 0x9FFF, PLA::CARTRIDGE_LO, 0x8000},
            {0xA000, 0xBFFF, PLA::UNMAPPED, 0},
            {0xC000, 0xCFFF, PLA::UNMAPPED, 0},
            {0xD000, 0xDFFF, PLA::IO, 0},
            {0xE000, 0xFFFF, PLA::CARTRIDGE_HI, 0xE000}
       }
    },
     // Mode 17: (exROM=1, game=0, charen=0, hiram=0, loram=1)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::UNMAPPED, 0},
            {0x8000, 0x9FFF, PLA::CARTRIDGE_LO, 0x8000},
            {0xA000, 0xBFFF, PLA::UNMAPPED, 0},
            {0xC000, 0xCFFF, PLA::UNMAPPED, 0},
            {0xD000, 0xDFFF, PLA::IO, 0},
            {0xE000, 0xFFFF, PLA::CARTRIDGE_HI, 0xE000}
       }
    },
     // Mode 18: (exROM=1, game=0, charen=0, hiram=1, loram=0)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::UNMAPPED, 0},
            {0x8000, 0x9FFF, PLA::CARTRIDGE_LO, 0x8000},
            {0xA000, 0xBFFF, PLA::UNMAPPED, 0},
            {0xC000, 0xCFFF, PLA::UNMAPPED, 0},
            {0xD000, 0xDFFF, PLA::IO, 0},
            {0xE000, 0xFFFF, PLA::CARTRIDGE_HI, 0xE000}
       }
    },
     // Mode 19: (exROM=1, game=0, charen=0, hiram=1, loram=1)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::UNMAPPED, 0},
            {0x8000, 0x9FFF, PLA::CARTRIDGE_LO, 0x8000},
            {0xA000, 0xBFFF, PLA::UNMAPPED, 0},
            {0xC000, 0xCFFF, PLA::UNMAPPED, 0},
            {0xD000, 0xDFFF, PLA::IO, 0},
            {0xE000, 0xFFFF, PLA::CARTRIDGE_HI, 0xE000}
       }
    },
     // Mode 20: (exROM=1, game=0, charen=1, hiram=0, loram=0)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::UNMAPPED, 0},
            {0x8000, 0x9FFF, PLA::CARTRIDGE_LO, 0x8000},
            {0xA000, 0xBFFF, PLA::UNMAPPED, 0},
            {0xC000, 0xCFFF, PLA::UNMAPPED, 0},
            {0xD000, 0xDFFF, PLA::IO, 0},
            {0xE000, 0xFFFF, PLA::CARTRIDGE_HI, 0xE000}
       }
    },
     // Mode 21: (exROM=1, game=0, charen=1, hiram=0, loram=1)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::UNMAPPED, 0},
            {0x8000, 0x9FFF, PLA::CARTRIDGE_LO, 0x8000},
            {0xA000, 0xBFFF, PLA::UNMAPPED, 0},
            {0xC000, 0xCFFF, PLA::UNMAPPED, 0},
            {0xD000, 0xDFFF, PLA::IO, 0},
            {0xE000, 0xFFFF, PLA::CARTRIDGE_HI, 0xE000}
       }
    },
     // Mode 22: (exROM=1, game=0, charen=1, hiram=1, loram=0)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::UNMAPPED, 0},
            {0x8000, 0x9FFF, PLA::CARTRIDGE_LO, 0x8000},
            {0xA000, 0xBFFF, PLA::UNMAPPED, 0},
            {0xC000, 0xCFFF, PLA::UNMAPPED, 0},
            {0xD000, 0xDFFF, PLA::IO, 0},
            {0xE000, 0xFFFF, PLA::CARTRIDGE_HI, 0xE000}
       }
    },
     // Mode 23: (exROM=1, game=0, charen=1, hiram=1, loram=1)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::UNMAPPED, 0},
            {0x8000, 0x9FFF, PLA::CARTRIDGE_LO, 0x8000},
            {0xA000, 0xBFFF, PLA::UNMAPPED, 0},
            {0xC000, 0xCFFF, PLA::UNMAPPED, 0},
            {0xD000, 0xDFFF, PLA::IO, 0},
            {0xE000, 0xFFFF, PLA::CARTRIDGE_HI, 0xE000}
       }
    },
    // Mode 24: (exROM=1, game=1, charen=0, hiram=0, loram=0)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::RAM, 0},
            {0x8000, 0x9FFF, PLA::RAM, 0},
            {0xA000, 0xBFFF, PLA::RAM, 0},
            {0xC000, 0xCFFF, PLA::RAM, 0},
            {0xD000, 0xDFFF, PLA::RAM, 0},
            {0xE000, 0xFFFF, PLA::RAM, 0}
       }
    },
    // Mode 25: (exROM=1, game=1, charen=0, hiram=0, loram=1)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::RAM, 0},
            {0x8000, 0x9FFF, PLA::RAM, 0},
            {0xA000, 0xBFFF, PLA::BASIC_ROM, 0xA000},
            {0xC000, 0xCFFF, PLA::RAM, 0},
            {0xD000, 0xDFFF, PLA::CHARACTER_ROM, 0xD000},
            {0xE000, 0xFFFF, PLA::RAM, 0}
       }
    },
    // Mode 26: (exROM=1, game=1, charen=0, hiram=1, loram=0)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::RAM, 0},
            {0x8000, 0x9FFF, PLA::RAM, 0},
            {0xA000, 0xBFFF, PLA::RAM, 0},
            {0xC000, 0xCFFF, PLA::RAM, 0},
            {0xD000, 0xDFFF, PLA::CHARACTER_ROM, 0xD000},
            {0xE000, 0xFFFF, PLA::KERNAL_ROM, 0xE000}
       }
    },
    // Mode 27: (exROM=1, game=1, charen=0, hiram=1, loram=1)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::RAM, 0},
            {0x8000, 0x9FFF, PLA::RAM, 0},
            {0xA000, 0xBFFF, PLA::BASIC_ROM, 0xA000},
            {0xC000, 0xCFFF, PLA::RAM, 0},
            {0xD000, 0xDFFF, PLA::CHARACTER_ROM, 0xD000},
            {0xE000, 0xFFFF, PLA::KERNAL_ROM, 0xE000}
       }
    },
    // Mode 28: (exROM=1, game=1, charen=1, hiram=0, loram=0)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::RAM, 0},
            {0x8000, 0x9FFF, PLA::RAM, 0},
            {0xA000, 0xBFFF, PLA::RAM, 0},
            {0xC000, 0xCFFF, PLA::RAM, 0},
            {0xD000, 0xDFFF, PLA::IO, 0},
            {0xE000, 0xFFFF, PLA::RAM, 0}
       }
    },
    // Mode 29: (exROM=1, game=1, charen=1, hiram=0, loram=1)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::RAM, 0},
            {0x8000, 0x9FFF, PLA::RAM, 0},
            {0xA000, 0xBFFF, PLA::RAM, 0},
            {0xC000, 0xCFFF, PLA::RAM, 0},
            {0xD000, 0xDFFF, PLA::IO, 0},
            {0xE000, 0xFFFF, PLA::RAM, 0}
       }
    },
    // Mode 30: (exROM=1, game=1, charen=1, hiram=1, loram=0)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::RAM, 0},
            {0x8000, 0x9FFF, PLA::RAM, 0},
            {0xA000, 0xBFFF, PLA::RAM, 0},
            {0xC000, 0xCFFF, PLA::RAM, 0},
            {0xD000, 0xDFFF, PLA::IO, 0},
            {0xE000, 0xFFFF, PLA::KERNAL_ROM, 0xE000}
       }
    },
    // Mode 31: (exROM=1, game=1, charen=1, hiram=1, loram=1)
    {
       {
            {0x0000, 0x0FFF, PLA::RAM, 0},
            {0x1000, 0x7FFF, PLA::RAM, 0},
            {0x8000, 0x9FFF, PLA::RAM, 0},
            {0xA000, 0xBFFF, PLA::BASIC_ROM, 0xA000},
            {0xC000, 0xCFFF, PLA::RAM, 0},
            {0xD000, 0xDFFF, PLA::IO, 0},
            {0xE000, 0xFFFF, PLA::KERNAL_ROM, 0xE000}
       }
    },
};

const PLAMapper::modeMapping* PLAMapper::getMappings() {
    return mappings;
}
