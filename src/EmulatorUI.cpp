// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <fstream>
#include "EmulatorUI.h"

EmulatorUI::EmulatorUI() :
    pendingDevice_(8),
    pendingDriveType_(UiCommand::DriveType::D1541)
{
    fileDlg.open = false;
    fileDlg.currentDir = std::filesystem::current_path();
}

EmulatorUI::~EmulatorUI() = default;

void EmulatorUI::draw()
{
    MediaViewState snapshot;
    {
        std::lock_guard<std::mutex> lock(viewMutex_);
        snapshot = view_;
    }

    installMenu(snapshot);
}

std::vector<UiCommand> EmulatorUI::consumeCommands()
{
    std::lock_guard<std::mutex> lock(outMutex_);
    auto tmp = std::move(out_);
    out_.clear();
    return tmp;
}

void EmulatorUI::setMediaViewState(const MediaViewState & s)
{
    std::lock_guard<std::mutex> lock(viewMutex_);
    view_ = s;
}

void EmulatorUI::push(UiCommand::Type t, std::string path, int deviceNum, UiCommand::DriveType driveType)
{
    std::lock_guard<std::mutex> lock(outMutex_);
    UiCommand c;
    c.type = t;
    c.path = std::move(path);
    c.deviceNum = deviceNum;
    c.driveType = driveType;
    out_.push_back(std::move(c));
}

void EmulatorUI::startFileDialog(const char* title, std::initializer_list<const char*> exts, UiCommand::Type type)
{
    fileDlg.title = title ? title : "";
    fileDlg.allowedExtensions.clear();
    for (auto e : exts)
        fileDlg.allowedExtensions.emplace_back(e);

    fileDlg.selectedEntry.clear();
    fileDlg.fileName.clear();
    fileDlg.error.clear();

    fileDlg.mode = FileDialog::Mode::OpenExisting;
    fileDlg.open = true;

    pendingType_ = type;
}

void EmulatorUI::startSaveFileDialog(const char* title, std::initializer_list<const char*> exts, UiCommand::Type type, bool allowOverwrite)
{
    fileDlg.title = title ? title : "";
    fileDlg.allowedExtensions.clear();
    for (auto e : exts)
        fileDlg.allowedExtensions.emplace_back(e);

    fileDlg.selectedEntry.clear();
    fileDlg.fileName.clear();
    fileDlg.error.clear();

    fileDlg.allowOverwrite = allowOverwrite;
    fileDlg.mode = FileDialog::Mode::SaveAs;

    fileDlg.open = true;
    pendingType_ = type;
}

void EmulatorUI::startDiskFileDialog(int deviceNum, UiCommand::DriveType driveType)
{
    pendingType_      = UiCommand::Type::AttachDisk;
    pendingDevice_    = deviceNum;
    pendingDriveType_ = driveType;

    // Filter by drive type
    switch (driveType)
    {
        case UiCommand::DriveType::D1541:
            startFileDialog("Select D64 Image (1541)", { ".d64" }, UiCommand::Type::AttachDisk);
            break;

        case UiCommand::DriveType::D1571:
            startFileDialog("Select D64/D71 Image (1571)", { ".d64", ".d71" }, UiCommand::Type::AttachDisk);
            break;

        case UiCommand::DriveType::D1581:
            startFileDialog("Select D81 Image (1581)", { ".d81" }, UiCommand::Type::AttachDisk);
            break;
    }
}

void EmulatorUI::drawFileDialog()
{
    if (!fileDlg.open)
        return;

    // Put the dialog in the main viewport work area (excludes OS/task bars; includes menu bar handling nicely)
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 workPos  = vp->WorkPos;
    ImVec2 workSize = vp->WorkSize;

    // Margin so it never touches edges
    const float margin = 24.0f;

    // Desired dialog size (cap it so it fits)
    ImVec2 desired(760.0f, 520.0f);
    desired.x = std::min(desired.x, workSize.x - margin * 2.0f);
    desired.y = std::min(desired.y, workSize.y - margin * 2.0f);

    // Center in the work area when it appears
    ImVec2 center(workPos.x + workSize.x * 0.5f, workPos.y + workSize.y * 0.5f);

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(desired, ImGuiCond_Appearing);

    // Safety: never allow resizing beyond the work area
    ImGui::SetNextWindowSizeConstraints(ImVec2(420, 260),
                                        ImVec2(workSize.x - margin * 2.0f, workSize.y - margin * 2.0f));

    const char* windowTitle = fileDlg.title.empty() ? "Select File" : fileDlg.title.c_str();
    if (!ImGui::Begin(windowTitle, &fileDlg.open))
    {
        ImGui::End();
        return;
    }

    namespace fs = std::filesystem;

    auto openPath = [this](const fs::path& path)
    {
        try
        {
            if (fs::is_directory(path))
            {
                fileDlg.currentDir = path;
                fileDlg.selectedEntry.clear();
                return;
            }

            if (fileDlg.mode == FileDialog::Mode::SaveAs)
            {
                // SaveAs: adopt filename only (do not emit yet)
                fileDlg.fileName = path.filename().string();
                return;
            }

            // OpenExisting: accept file immediately if allowed
            if (!isAllowedByExtension(path))
            {
                fileDlg.error = "File type not allowed for this action.";
                return;
            }

            emitChosenPath(path);
        }
        catch (const std::exception& e)
        {
            fileDlg.error = e.what();
        }
    };

    // Header: current directory
    ImGui::TextUnformatted(fileDlg.currentDir.string().c_str());
    ImGui::Separator();

    // File list
    float reserve = 0.0f;

    // space for bottom row buttons
    reserve += ImGui::GetFrameHeightWithSpacing();

    // space for error line (even if not shown, reserve a little)
    reserve += ImGui::GetTextLineHeightWithSpacing();

    // space for SaveAs filename controls
    if (fileDlg.mode == FileDialog::Mode::SaveAs)
    {
        reserve += ImGui::GetTextLineHeightWithSpacing();   // "File name:"
        reserve += ImGui::GetFrameHeightWithSpacing();      // input
        reserve += ImGui::GetStyle().ItemSpacing.y;         // separator spacing
    }

    ImGui::BeginChild("##file_list", ImVec2(0, -reserve), true);
    std::vector<fs::directory_entry> entries;
    fileDlg.error.clear();

    try
    {
        for (const auto& entry : fs::directory_iterator(fileDlg.currentDir))
            entries.push_back(entry);

        std::sort(entries.begin(), entries.end(),
                  [](const fs::directory_entry& a, const fs::directory_entry& b)
                  {
                      bool ad = a.is_directory();
                      bool bd = b.is_directory();
                      if (ad != bd) return ad; // dirs first
                      return a.path().filename().string() < b.path().filename().string();
                  });
    }
    catch (const std::exception& e)
    {
        fileDlg.error = e.what();
    }

    // Parent folder
    if (ImGui::Selectable("..", false, ImGuiSelectableFlags_AllowDoubleClick))
    {
        auto parent = fileDlg.currentDir.parent_path();
        if (!parent.empty())
        {
            fileDlg.currentDir = parent;
            fileDlg.selectedEntry.clear();
        }
    }

    for (const auto& entry : entries)
    {
        const fs::path& path = entry.path();
        std::string name = path.filename().string();
        bool isDir = entry.is_directory();

        // Only filter visible files in OpenExisting mode (SaveAs should show everything)
        if (!isDir && fileDlg.mode == FileDialog::Mode::OpenExisting)
        {
            if (!isAllowedByExtension(path))
                continue;
        }

        std::string label = isDir ? (name + "/") : name;
        bool selected = (fileDlg.selectedEntry == name);

        if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick))
        {
            // Always update selection
            fileDlg.selectedEntry = name;

            // SaveAs: single click fills the filename box
            if (fileDlg.mode == FileDialog::Mode::SaveAs && !isDir)
                fileDlg.fileName = name;

            // Double-click action
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                openPath(path);
            }
        }
    }

    ImGui::EndChild();

    // SaveAs filename input
    if (fileDlg.mode == FileDialog::Mode::SaveAs)
    {
        ImGui::Separator();
        ImGui::TextUnformatted("File name:");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##save_name", &fileDlg.fileName);
    }

    // Error display
    if (!fileDlg.error.empty())
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "%s", fileDlg.error.c_str());

    // Bottom buttons
    if (ImGui::Button("Up"))
    {
        auto parent = fileDlg.currentDir.parent_path();
        if (!parent.empty())
        {
            fileDlg.currentDir = parent;
            fileDlg.selectedEntry.clear();
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel"))
    {
        fileDlg.open = false;
        ImGui::End();
        return;
    }

    ImGui::SameLine();

    if (fileDlg.mode == FileDialog::Mode::OpenExisting)
    {
        bool hasSelection = !fileDlg.selectedEntry.empty();
        if (!hasSelection) ImGui::BeginDisabled();

        if (ImGui::Button("Open"))
        {
            try
            {
                fs::path p = fileDlg.currentDir / fileDlg.selectedEntry;

                if (fs::is_directory(p))
                {
                    fileDlg.currentDir = p;
                    fileDlg.selectedEntry.clear();
                }
                else
                {
                    if (!isAllowedByExtension(p))
                        fileDlg.error = "File type not allowed for this action.";
                    else
                        emitChosenPath(p);
                }
            }
            catch (const std::exception& e)
            {
                fileDlg.error = e.what();
            }
        }

        if (!hasSelection) ImGui::EndDisabled();
    }
    else // SaveAs
    {
        bool hasName = !fileDlg.fileName.empty();
        if (!hasName) ImGui::BeginDisabled();

        if (ImGui::Button("Save"))
        {
            try
            {
                fs::path outPath = fileDlg.currentDir / fileDlg.fileName;

                // Auto-append extension if none and exactly one allowed ext
                if (!outPath.has_extension() && fileDlg.allowedExtensions.size() == 1)
                    outPath += fileDlg.allowedExtensions[0];

                if (!isAllowedByExtension(outPath))
                {
                    fileDlg.error = "File type not allowed for this action.";
                }
                else if (fs::exists(outPath) && !fileDlg.allowOverwrite)
                {
                    fileDlg.error = "File already exists. Choose a different name.";
                }
                else
                {
                    emitChosenPath(outPath);
                }
            }
            catch (const std::exception& e)
            {
                fileDlg.error = e.what();
            }
        }

        if (!hasName) ImGui::EndDisabled();
    }

    ImGui::End();
}

void EmulatorUI::installMenu(const MediaViewState& v)
{
    static bool aboutRequested = false;

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::BeginMenu("Attach Disk Image..."))
            {
                for (int dev = 8; dev <= 11; ++dev)
                {
                    char label[32];
                    std::snprintf(label, sizeof(label), "Drive %d", dev);

                    if (ImGui::BeginMenu(label))
                    {
                        if (ImGui::MenuItem("1541 (D64)"))
                            startDiskFileDialog(dev, UiCommand::DriveType::D1541);

                        if (ImGui::MenuItem("1571 (D64/D71)"))
                            startDiskFileDialog(dev, UiCommand::DriveType::D1571);

                        if (ImGui::MenuItem("1581 (D81)"))
                            startDiskFileDialog(dev, UiCommand::DriveType::D1581);

                        ImGui::EndMenu();
                    }
                }
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Attach PRG/P00 image..."))
                startFileDialog("Select PRG/P00 Image", { ".prg", ".p00" }, UiCommand::Type::AttachPRG);

            if (ImGui::MenuItem("Attach Cartridge image..."))
                startFileDialog("Select CRT Image", { ".crt" }, UiCommand::Type::AttachCRT);

            if (ImGui::MenuItem("Attach T64 image..."))
                startFileDialog("Select T64 image", { ".t64" }, UiCommand::Type::AttachT64);

            if (ImGui::MenuItem("Attach TAP image..."))
                startFileDialog("Select TAP image", { ".tap" }, UiCommand::Type::AttachTAP);

            ImGui::Separator();

            if (ImGui::BeginMenu("Cassette Control"))
            {
                if (ImGui::MenuItem("Play", "Alt+P"))   push(UiCommand::Type::CassPlay);
                if (ImGui::MenuItem("Stop", "Alt+S"))   push(UiCommand::Type::CassStop);
                if (ImGui::MenuItem("Rewind", "Alt+R")) push(UiCommand::Type::CassRewind);
                if (ImGui::MenuItem("Eject", "Alt+E"))  push(UiCommand::Type::CassEject);
                ImGui::EndMenu();
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Alt+F4")) push(UiCommand::Type::Quit);

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Input"))
        {
            bool j1 = v.joy1Attached;
            bool j2 = v.joy2Attached;
            if (ImGui::MenuItem("Joystick 1 Attached", nullptr, j1)) push(UiCommand::Type::ToggleJoy1);
            if (ImGui::MenuItem("Joystick 2 Attached", nullptr, j2)) push(UiCommand::Type::ToggleJoy2);
            ImGui::Separator();
            ImGui::TextUnformatted("Gamepad Routing");
            ImGui::Text("Pad1: %s", v.pad1Name.c_str());
            ImGui::Text("Pad2: %s", v.pad2Name.c_str());

            if (ImGui::MenuItem("Assign Pad1 -> Port 1")) push(UiCommand::Type::AssignPad1ToPort1);
            if (ImGui::MenuItem("Assign Pad1 -> Port 2")) push(UiCommand::Type::AssignPad1ToPort2);
            if (ImGui::MenuItem("Assign Pad2 -> Port 1")) push(UiCommand::Type::AssignPad2ToPort1);
            if (ImGui::MenuItem("Assign Pad2 -> Port 2")) push(UiCommand::Type::AssignPad2ToPort2);

            if (ImGui::MenuItem("Clear Port 1 Pad"))    push(UiCommand::Type::ClearPort1Pad);
            if (ImGui::MenuItem("Clear Port 2 Pad"))    push(UiCommand::Type::ClearPort2Pad);
            if (ImGui::MenuItem("Swap Port 1/2 Pads"))  push(UiCommand::Type::SwapPortPads);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("System"))
        {
            if (ImGui::MenuItem("Save Emulator State to File...", "Ctrl+S"))
                startSaveFileDialog("Save Emulator State (.sav)", { ".sav" }, UiCommand::Type::SaveState, true);
            if (ImGui::MenuItem("Load Emulator State from file...", "Ctrl+L"))
                startFileDialog("Select SAV image to load", { ".sav" }, UiCommand::Type::LoadState);

            ImGui::Separator();

            if (ImGui::MenuItem("Warm Reset", "Ctrl+W"))       push(UiCommand::Type::WarmReset);
            if (ImGui::MenuItem("Cold Reset", "Ctrl+Shift+R")) push(UiCommand::Type::ColdReset);

            ImGui::Separator();

            bool isPAL = v.pal;
            if (ImGui::MenuItem("NTSC", nullptr, !isPAL)) push(UiCommand::Type::SetNTSC);
            if (ImGui::MenuItem("PAL",  nullptr,  isPAL)) push(UiCommand::Type::SetPAL);

            ImGui::Separator();

            bool paused = v.paused;
            if (ImGui::MenuItem(paused ? "Resume" : "Pause", "Ctrl+Space")) push(UiCommand::Type::TogglePause);

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("About")) aboutRequested = true;
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    if (aboutRequested) { ImGui::OpenPopup("About C64 Emulator"); aboutRequested = false; }

    if (ImGui::BeginPopupModal("About C64 Emulator", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("C64 Emulator - ImGui Menu Overlay");
        ImGui::Separator();
        ImGui::Text("F12 opens ML Monitor.\nName: %s\nVersion: %s" , VersionInfo::NAME, VersionInfo::VERSION);
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    drawFileDialog();
}

bool EmulatorUI::isAllowedByExtension(const std::filesystem::path& path) const
{
    if (fileDlg.allowedExtensions.empty())
        return true;

    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    for (const auto& aRaw : fileDlg.allowedExtensions)
    {
        std::string a = aRaw;
        std::transform(a.begin(), a.end(), a.begin(), ::tolower);
        if (ext == a)
            return true;
    }
    return false;
}

void EmulatorUI::emitChosenPath(const std::filesystem::path& path)
{
    if (pendingType_ == UiCommand::Type::AttachDisk)
        push(pendingType_, path.string(), pendingDevice_, pendingDriveType_);
    else
        push(pendingType_, path.string());

    fileDlg.open = false;
    fileDlg.selectedEntry.clear();
    fileDlg.fileName.clear();
    fileDlg.error.clear();
}
