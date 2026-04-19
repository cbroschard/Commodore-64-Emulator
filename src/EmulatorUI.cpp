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
    drawDriveStatus(snapshot);
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

void EmulatorUI::pushSetCartSwitch(uint32_t switchIndex, uint32_t switchPos)
{
    std::lock_guard<std::mutex> lock(outMutex_);
    UiCommand c;
    c.type = UiCommand::Type::SetCartSwitch;
    c.switchIndex = switchIndex;
    c.switchPos = switchPos;
    out_.push_back(std::move(c));
}

void EmulatorUI::pushCartButton(uint32_t buttonIndex)
{
    std::lock_guard<std::mutex> lock(outMutex_);
    UiCommand c;
    c.type = UiCommand::Type::PressButton;
    c.buttonIndex = buttonIndex;
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
    fileDlg.lastClickedEntry.clear();
    fileDlg.lastClickTime = std::chrono::steady_clock::time_point{};

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
    fileDlg.lastClickedEntry.clear();
    fileDlg.lastClickTime = std::chrono::steady_clock::time_point{};

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

        case UiCommand::DriveType::None:
        default:
            return;
    }
}

void EmulatorUI::startCreateBlankDiskDialog(int deviceNum, UiCommand::DriveType driveType)
{
    pendingType_      = UiCommand::Type::CreateBlankDisk;
    pendingDevice_    = deviceNum;
    pendingDriveType_ = driveType;

    switch (driveType)
    {
        case UiCommand::DriveType::D1541:
            startSaveFileDialog("Create Blank D64 Image (1541)",
                                { ".d64" },
                                UiCommand::Type::CreateBlankDisk,
                                false);
            break;

        case UiCommand::DriveType::D1571:
            startSaveFileDialog("Create Blank D71 Image (1571)",
                                { ".d71" },
                                UiCommand::Type::CreateBlankDisk,
                                false);
            break;

        case UiCommand::DriveType::D1581:
            startSaveFileDialog("Create Blank D81 Image (1581)",
                                { ".d81" },
                                UiCommand::Type::CreateBlankDisk,
                                false);
            break;

        case UiCommand::DriveType::None:
        default:
            return;
    }
}

void EmulatorUI::drawFileDialog()
{
    if (!fileDlg.open)
        return;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 workPos  = vp->WorkPos;
    ImVec2 workSize = vp->WorkSize;

    const float margin = 24.0f;

    ImVec2 desired(760.0f, 520.0f);
    desired.x = std::min(desired.x, workSize.x - margin * 2.0f);
    desired.y = std::min(desired.y, workSize.y - margin * 2.0f);

    ImVec2 center(workPos.x + workSize.x * 0.5f, workPos.y + workSize.y * 0.5f);

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(desired, ImGuiCond_Appearing);
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
                fileDlg.error.clear();
                fileDlg.lastClickedEntry.clear();
                fileDlg.lastClickTime = std::chrono::steady_clock::time_point{};
                return;
            }

            if (fileDlg.mode == FileDialog::Mode::SaveAs)
            {
                fileDlg.fileName = path.filename().string();
                return;
            }

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

    ImGui::TextUnformatted(fileDlg.currentDir.string().c_str());
    ImGui::Separator();

    float reserve = 0.0f;
    reserve += ImGui::GetFrameHeightWithSpacing();
    reserve += ImGui::GetTextLineHeightWithSpacing();

    if (fileDlg.mode == FileDialog::Mode::SaveAs)
    {
        reserve += ImGui::GetTextLineHeightWithSpacing();
        reserve += ImGui::GetFrameHeightWithSpacing();
        reserve += ImGui::GetStyle().ItemSpacing.y;
    }

    ImGui::BeginChild("##file_list", ImVec2(0, -reserve), true);

    std::vector<fs::directory_entry> entries;

    try
    {
        for (const auto& entry : fs::directory_iterator(fileDlg.currentDir))
            entries.push_back(entry);

        std::sort(entries.begin(), entries.end(),
                  [](const fs::directory_entry& a, const fs::directory_entry& b)
                  {
                      bool ad = a.is_directory();
                      bool bd = b.is_directory();
                      if (ad != bd) return ad;
                      return a.path().filename().string() < b.path().filename().string();
                  });
    }
    catch (const std::exception& e)
    {
        fileDlg.error = e.what();
    }

    if (ImGui::Selectable("..", false))
    {
        const auto now = std::chrono::steady_clock::now();
        const bool sameItem = (fileDlg.lastClickedEntry == "..");
        const auto deltaMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - fileDlg.lastClickTime).count();

        if (sameItem && deltaMs >= 0 && deltaMs <= 500)
        {
            auto parent = fileDlg.currentDir.parent_path();
            if (!parent.empty())
            {
                fileDlg.currentDir = parent;
                fileDlg.selectedEntry.clear();
                fileDlg.error.clear();
            }
            fileDlg.lastClickedEntry.clear();
            fileDlg.lastClickTime = std::chrono::steady_clock::time_point{};
        }
        else
        {
            fileDlg.lastClickedEntry = "..";
            fileDlg.lastClickTime = now;
        }
    }

    for (const auto& entry : entries)
    {
        const fs::path& path = entry.path();
        std::string name = path.filename().string();
        bool isDir = entry.is_directory();

        if (!isDir && fileDlg.mode == FileDialog::Mode::OpenExisting)
        {
            if (!isAllowedByExtension(path))
                continue;
        }

        std::string label = isDir ? (name + "/") : name;
        bool selected = (fileDlg.selectedEntry == name);

        if (ImGui::Selectable(label.c_str(), selected))
        {
            const auto now = std::chrono::steady_clock::now();
            const bool sameItem = (fileDlg.lastClickedEntry == name);
            const auto deltaMs =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - fileDlg.lastClickTime).count();

            fileDlg.selectedEntry = name;
            fileDlg.error.clear();

            if (fileDlg.mode == FileDialog::Mode::SaveAs && !isDir)
                fileDlg.fileName = name;

            if (sameItem && deltaMs >= 0 && deltaMs <= 500)
            {
                openPath(path);
                fileDlg.lastClickedEntry.clear();
                fileDlg.lastClickTime = std::chrono::steady_clock::time_point{};
            }
            else
            {
                fileDlg.lastClickedEntry = name;
                fileDlg.lastClickTime = now;
            }
        }
    }

    ImGui::EndChild();

    if (fileDlg.mode == FileDialog::Mode::SaveAs)
    {
        ImGui::Separator();
        ImGui::TextUnformatted("File name:");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##save_name", &fileDlg.fileName);
    }

    if (!fileDlg.error.empty())
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "%s", fileDlg.error.c_str());

    if (ImGui::Button("Up"))
    {
        auto parent = fileDlg.currentDir.parent_path();
        if (!parent.empty())
        {
            fileDlg.currentDir = parent;
            fileDlg.selectedEntry.clear();
            fileDlg.error.clear();
            fileDlg.lastClickedEntry.clear();
            fileDlg.lastClickTime = std::chrono::steady_clock::time_point{};
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel"))
    {
        fileDlg.open = false;
        fileDlg.lastClickedEntry.clear();
        fileDlg.lastClickTime = std::chrono::steady_clock::time_point{};
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
                    fileDlg.error.clear();
                    fileDlg.lastClickedEntry.clear();
                    fileDlg.lastClickTime = std::chrono::steady_clock::time_point{};
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
    else
    {
        bool hasName = !fileDlg.fileName.empty();
        if (!hasName) ImGui::BeginDisabled();

        if (ImGui::Button("Save"))
        {
            try
            {
                fs::path outPath = fileDlg.currentDir / fileDlg.fileName;

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
            if (ImGui::MenuItem("Load Program..."))
                startFileDialog("Select PRG/P00 Image", { ".prg", ".p00" }, UiCommand::Type::AttachPRG);

            ImGui::Separator();

            if (ImGui::BeginMenu("Disk"))
            {
                drawDriveDiskMenu(v, 8);
                drawDriveDiskMenu(v, 9);
                drawDriveDiskMenu(v, 10);
                drawDriveDiskMenu(v, 11);

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Cartridge"))
            {
                if (ImGui::MenuItem("Attach Cartridge image..."))
                    startFileDialog("Select CRT Image", { ".crt" }, UiCommand::Type::AttachCRT);

                if (ImGui::MenuItem("Eject Cartridge...", nullptr, false, v.cartAttached))
                    push(UiCommand::Type::EjectCRT);

                ImGui::EndMenu();
            }

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

        if (!v.cartSwitches.empty() || !v.cartButtons.empty())
        {
            if (ImGui::BeginMenu("Cartridge"))
            {
                // Cartridge switches (dynamic)
                if (!v.cartSwitches.empty())
                {
                    ImGui::Separator();

                    for (uint32_t si = 0; si < static_cast<uint32_t>(v.cartSwitches.size()); si++)
                    {
                        const auto& sw = v.cartSwitches[si];

                        if (ImGui::BeginMenu(sw.name.c_str()))
                        {
                            for (uint32_t p = 0; p < static_cast<uint32_t>(sw.positions.size()); p++)
                            {
                                bool selected = (sw.currentPos == p);
                                if (ImGui::MenuItem(sw.positions[p].c_str(), nullptr, selected))
                                {
                                    pushSetCartSwitch(si, p);
                                }
                            }
                            ImGui::EndMenu();
                        }
                    }
                }

                if (!v.cartButtons.empty())
                {
                    ImGui::Separator();

                    for (uint32_t bi = 0; bi < static_cast<uint32_t>(v.cartButtons.size()); ++bi)
                    {
                        const auto& bu = v.cartButtons[bi];

                        if (!bu.enabled)
                            ImGui::BeginDisabled();

                        if (ImGui::MenuItem(bu.name.c_str()))
                            pushCartButton(static_cast<uint32_t>(bu.index));

                        if (!bu.enabled)
                            ImGui::EndDisabled();
                    }
                }

                ImGui::EndMenu();
            }
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
    if (pendingType_ == UiCommand::Type::AttachDisk || pendingType_ == UiCommand::Type::CreateBlankDisk)
        push(pendingType_, path.string(), pendingDevice_, pendingDriveType_);
    else
        push(pendingType_, path.string());

    fileDlg.open = false;
    fileDlg.selectedEntry.clear();
    fileDlg.fileName.clear();
    fileDlg.error.clear();
    fileDlg.lastClickedEntry.clear();
    fileDlg.lastClickTime = std::chrono::steady_clock::time_point{};
}

void EmulatorUI::drawDriveStatus(const MediaViewState& v)
{
    bool anyPresent = false;
    for (const auto& drive : v.drives)
    {
        if (drive.present)
        {
            anyPresent = true;
            break;
        }
    }

    if (!anyPresent)
        return;

    ImGuiViewport* vp = ImGui::GetMainViewport();

    const float marginX = 8.0f;

    // Smaller = lower. 0 puts it very close to the bottom.
    const float marginY = 0.0f;

    // Use full viewport bottom instead of WorkSize bottom so it sits slightly lower.
    float nextX = vp->Pos.x + marginX;
    const float bottomY = vp->Pos.y + vp->Size.y - marginY;

    const float gapX = 8.0f;

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings;

    for (const auto& drive : v.drives)
    {
        if (!drive.present)
            continue;

        ImGui::SetNextWindowPos(
            ImVec2(nextX, bottomY),
            ImGuiCond_Always,
            ImVec2(0.0f, 1.0f) // bottom-left anchor
        );

        ImGui::SetNextWindowBgAlpha(0.85f);

        char windowName[64];
        std::snprintf(windowName, sizeof(windowName), "##DriveStatus%d", drive.deviceNum);

        ImVec2 thisWindowSize(0.0f, 0.0f);

        if (ImGui::Begin(windowName, nullptr, flags))
        {
            if (!drive.modelName.empty())
                ImGui::Text("Drive %d (%s)", drive.deviceNum, drive.modelName.c_str());
            else
                ImGui::Text("Drive %d", drive.deviceNum);

            if (!drive.lights.empty())
                drawDriveLights(drive);

            if (drive.diskInserted)
            {
                if (drive.hasTrackSector)
                    ImGui::Text("Track/Sector: %d / %d", drive.track, drive.sector);
                else
                    ImGui::TextUnformatted("Track/Sector: -- / --");
            }
            else
            {
                ImGui::TextUnformatted("No disk inserted");
            }

            // Important: capture size while this window is still active.
            thisWindowSize = ImGui::GetWindowSize();
        }

        ImGui::End();

        // Put the next drive immediately to the right of the one we just drew.
        nextX += thisWindowSize.x + gapX;
    }
}

void EmulatorUI::drawDriveLights(const DriveStatusView& drive)
{
    for (size_t i = 0; i < drive.lights.size(); ++i)
    {
        const auto& light = drive.lights[i];

        ImGui::TextUnformatted(light.name.c_str());
        ImGui::SameLine();

        ImVec2 p = ImGui::GetCursorScreenPos();
        const float radius = 5.0f;
        ImVec2 center(p.x + radius, p.y + radius + 2.0f);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddCircleFilled(center, radius, toImGuiColor(light.color, light.on));
        dl->AddCircle(center, radius, IM_COL32(0, 0, 0, 255), 0, 1.0f);

        ImGui::Dummy(ImVec2(radius * 2.0f + 4.0f, radius * 2.0f + 4.0f));

        if (i + 1 < drive.lights.size())
            ImGui::SameLine();
    }
}

ImU32 EmulatorUI::toImGuiColor(EmulatorUI::DriveLightColor color, bool on)
{
    if (!on)
        return IM_COL32(40, 40, 40, 255);

    switch (color)
    {
        case EmulatorUI::DriveLightColor::Green:    return IM_COL32(0, 220, 80, 255);
        case EmulatorUI::DriveLightColor::Red:      return IM_COL32(255, 60, 60, 255);
        case EmulatorUI::DriveLightColor::Yellow:   return IM_COL32(255, 220, 60, 255);

        case EmulatorUI::DriveLightColor::Amber:    return IM_COL32(255, 180, 60, 255);
        default:                                    return IM_COL32(200, 200, 200, 255);
    }
}

EmulatorUI::DriveLightColor EmulatorUI::toUiColor(IDriveIndicatorView::DriveIndicatorColor c)
{
    switch (c)
    {
        case IDriveIndicatorView::DriveIndicatorColor::Green:  return EmulatorUI::DriveLightColor::Green;
        case IDriveIndicatorView::DriveIndicatorColor::Red:    return EmulatorUI::DriveLightColor::Red;
        case IDriveIndicatorView::DriveIndicatorColor::Yellow: return EmulatorUI::DriveLightColor::Yellow;
        case IDriveIndicatorView::DriveIndicatorColor::Amber:  return EmulatorUI::DriveLightColor::Amber;
        default:                                               return EmulatorUI::DriveLightColor::Green;
    }
}

void EmulatorUI::drawDriveDiskMenu(const MediaViewState& v, int dev)
{
    char label[32];
    std::snprintf(label, sizeof(label), "Drive %d", dev);

    if (ImGui::BeginMenu(label))
    {
        const UiCommand::DriveType existingType = getDriveTypeForDev(v, dev);
        const bool drivePresent = existingType != UiCommand::DriveType::None;

        const bool canUse1541 = !drivePresent || existingType == UiCommand::DriveType::D1541;
        const bool canUse1571 = !drivePresent || existingType == UiCommand::DriveType::D1571;
        const bool canUse1581 = !drivePresent || existingType == UiCommand::DriveType::D1581;

        if (ImGui::BeginMenu("Attach Disk Image..."))
        {
            if (ImGui::MenuItem("1541 (D64)", nullptr, false, canUse1541))
                startDiskFileDialog(dev, UiCommand::DriveType::D1541);

            if (ImGui::MenuItem("1571 (D64/D71)", nullptr, false, canUse1571))
                startDiskFileDialog(dev, UiCommand::DriveType::D1571);

            if (ImGui::MenuItem("1581 (D81)", nullptr, false, canUse1581))
                startDiskFileDialog(dev, UiCommand::DriveType::D1581);

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Create Blank Disk"))
        {
            if (ImGui::MenuItem("D64", nullptr, false, canUse1541))
                startCreateBlankDiskDialog(dev, UiCommand::DriveType::D1541);

            if (ImGui::MenuItem("D71", nullptr, false, canUse1571))
                startCreateBlankDiskDialog(dev, UiCommand::DriveType::D1571);

            if (ImGui::MenuItem("D81", nullptr, false, canUse1581))
                startCreateBlankDiskDialog(dev, UiCommand::DriveType::D1581);

            ImGui::EndMenu();
        }

        const bool hasDisk = driveHasDisk(v, dev);

        if (ImGui::MenuItem("Eject Disk", nullptr, false, hasDisk))
            pushEjectDisk(dev);

        ImGui::EndMenu();
    }
}

bool EmulatorUI::driveHasDisk(const MediaViewState& v, int dev)
{
    for (const auto& ds : v.drives)
    {
        if (ds.deviceNum == dev)
            return ds.present && ds.diskInserted;
    }

    return false;
}

void EmulatorUI::pushEjectDisk(int deviceNum)
{
    push
    (
        UiCommand::Type::EjectDisk,
        std::string{},
        deviceNum,
        UiCommand::DriveType::D1541 // placeholder; ignored for eject
    );
}

UiCommand::DriveType EmulatorUI::getDriveTypeForDev(const MediaViewState& v, int dev) const
{
    for (const auto& ds : v.drives)
    {
        if (ds.deviceNum == dev && ds.present)
            return ds.driveType;
    }

    return UiCommand::DriveType::None;
}
