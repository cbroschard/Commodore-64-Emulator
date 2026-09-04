// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef STATEMANAGER_H
#define STATEMANAGER_H

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include "Common/VideoMode.h"
#include "StateReader.h"
#include "StateWriter.h"

struct MachineComponents;
struct MachineRuntimeState;

class StateManager
{
    public:
        StateManager(MachineComponents& components, MachineRuntimeState& runtime);

        ~StateManager();

        bool save(const std::string& path);
        bool load(const std::string& path);

    private:
        MachineComponents& components_;
        MachineRuntimeState runtime_;

        static constexpr uint32_t kStateVersion = 1; // Save State file version

        bool loadInternal(const std::string& path);
};

#endif // STATEMANAGER_H
