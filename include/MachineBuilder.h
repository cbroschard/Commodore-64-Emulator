// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef MACHINEBUILDER_H
#define MACHINEBUILDER_H

class Computer;
struct MachineComponents;
struct MachineRuntimeState;
struct MachineRomConfig;

class MachineBuilder
{
    public:
        MachineBuilder();
        virtual ~MachineBuilder();

        static void assemble(Computer* host, MachineComponents& components_, MachineRuntimeState& runtime, MachineRomConfig& roms);
};

#endif // MACHINEBUILDER_H
