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
#include <chrono>
#include <filesystem>
#include <initializer_list>
#include <mutex>
#include <string>
#include <vector>
#include "imgui/imgui.h"
#include "imgui/misc/cpp/imgui_stdlib.h"
#include "UiCommand.h"
#include "Version.h"

class EmulatorUI
{
    public:
        EmulatorUI();
        virtual ~EmulatorUI();

        void draw();

        // Computer pulls and clears commands each frame
        std::vector<UiCommand> consumeCommands();

        // Handle cartridges that have physical switches and buttons
        struct CartSwitchView
        {
            std::string name;
            std::vector<std::string> positions;
            uint32_t currentPos = 0;
        };

        struct CartButtonView
        {
            uint32_t index = 0;
            std::string name;
            bool enabled = true;
        };

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

            std::vector<CartSwitchView> cartSwitches;
            std::vector<CartButtonView> cartButtons;
        };

        void setMediaViewState(const MediaViewState& s);

        inline bool isFileDialogOpen() const { return fileDlg.open; }

    protected:

    private:

        bool fileDialogOpen_ = false;
        std::string pendingPath_;
        UiCommand::Type pendingType_;

        int pendingDevice_;
        UiCommand::DriveType pendingDriveType_;

        bool joy1Attached;
        bool joy2Attached;
        std::string pad1Name;
        std::string pad2Name;

        struct FileDialog
        {
            enum class Mode
            {
                OpenExisting,
                SaveAs
            };

            bool open = false;
            bool allowOverwrite = false;
            Mode mode = Mode::OpenExisting;

            std::string title;
            std::filesystem::path currentDir;
            std::vector<std::string> allowedExtensions;

            std::string selectedEntry;
            std::string fileName;
            std::string error;

            // Manual double-click tracking
            std::string lastClickedEntry;
            std::chrono::steady_clock::time_point lastClickTime{};
        };
        FileDialog fileDlg;

        std::vector<UiCommand> out_;
        mutable std::mutex outMutex_;

        MediaViewState view_;
        mutable std::mutex viewMutex_;

        void installMenu(const MediaViewState& v);
        void startFileDialog(const char* title, std::initializer_list<const char*> exts, UiCommand::Type type);
        void startSaveFileDialog(const char* title, std::initializer_list<const char*> exts, UiCommand::Type type, bool allowOverwrite = false);
        void startDiskFileDialog(int deviceNum, UiCommand::DriveType driveType);
        void drawFileDialog();

        void push(UiCommand::Type t, std::string path = {}, int deviceNum = 8, UiCommand::DriveType driveType = UiCommand::DriveType::D1541);

        void pushSetCartSwitch(uint32_t switchIndex, uint32_t switchPos);
        void pushCartButton(uint32_t buttonIndex);

        bool isAllowedByExtension(const std::filesystem::path& path) const;
        void emitChosenPath(const std::filesystem::path& path);
};

#endif // EMULATORUI_H
