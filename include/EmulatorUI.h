// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef EMULATORUI_H
#define EMULATORUI_H

#include <algorithm>
#include <filesystem>
#include <initializer_list>
#include <mutex>
#include <string>
#include <vector>
#include "imgui/imgui.h"
#include "UiCommand.h"

class EmulatorUI
{
    public:
        EmulatorUI();
        virtual ~EmulatorUI();

        void draw();

        // Computer pulls and clears commands each frame
        std::vector<UiCommand> consumeCommands();

        struct MediaViewState
        {
            bool diskAttached    = false;       std::string diskPath;
            bool cartAttached    = false;       std::string cartPath;
            bool tapeAttached    = false;       std::string tapePath;
            bool prgAttached     = false;       std::string prgPath;

            bool joy1Attached    = false;
            bool joy2Attached    = false;

            std::string pad1Name = "None";
            std::string pad2Name = "None";

            bool paused          = false;
            bool pal             = true;
        };

        void setMediaViewState(const MediaViewState& s);

    protected:

    private:

        bool fileDialogOpen_ = false;
        std::string pendingPath_;
        UiCommand::Type pendingType_;

        bool joy1Attached;
        bool joy2Attached;
        std::string pad1Name;
        std::string pad2Name;

        struct FileDialog
        {
            bool open = false;
            std::string title;
            std::filesystem::path currentDir;
            std::vector<std::string> allowedExtensions;
            std::string selectedEntry;
            std::string error;
        };
        FileDialog fileDlg;

        std::vector<UiCommand> out_;
        mutable std::mutex outMutex_;

        MediaViewState view_;
        mutable std::mutex viewMutex_;

        void installMenu(const MediaViewState& v);
        void startFileDialog(const char* title, std::initializer_list<const char*> exts, UiCommand::Type type);
        void drawFileDialog();

        void push(UiCommand::Type t, std::string path = {});
};

#endif // EMULATORUI_H
