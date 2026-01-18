// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "EmulatorUI.h"

EmulatorUI::EmulatorUI()
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

void EmulatorUI::push(UiCommand::Type t, std::string path)
{
    std::lock_guard<std::mutex> lock(outMutex_);
    out_.push_back(UiCommand{ t, std::move(path) });
}

void EmulatorUI::startFileDialog(const char* title, std::initializer_list<const char*> exts, UiCommand::Type type)
{
    fileDlg.title = title;
    fileDlg.allowedExtensions.clear();
    for (auto e : exts)
        fileDlg.allowedExtensions.emplace_back(e);

    fileDlg.selectedEntry.clear();
    fileDlg.error.clear();
    fileDlg.open = true;

    pendingType_ = type;
}

void EmulatorUI::drawFileDialog()
{
    if (!fileDlg.open)
        return;

    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);

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
                fileDlg.currentDir    = path;
                fileDlg.selectedEntry.clear();
            }
            else
            {
                std::string ext = path.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                bool allowed = fileDlg.allowedExtensions.empty();
                if (!allowed)
                {
                    for (const auto& a : fileDlg.allowedExtensions)
                    {
                        std::string lower = a;
                        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                        if (ext == lower)
                        {
                            allowed = true;
                            break;
                        }
                    }
                }

                if (!allowed)
                {
                    fileDlg.error = "File type not allowed for this action.";
                }
                else
                {
                    push(pendingType_, path.string());
                    fileDlg.open = false;
                    fileDlg.selectedEntry.clear();
                }
            }
        }
        catch (const std::exception& e)
        {
            fileDlg.error = e.what();
        }
    };

    std::string pathStr = fileDlg.currentDir.string();
    ImGui::TextUnformatted(pathStr.c_str());
    ImGui::Separator();

    ImGui::BeginChild("##file_list", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()*2), true);

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
                      if (ad != bd) return ad;   // dirs first
                      return a.path().filename().string() < b.path().filename().string();
                  });
    }
    catch (const std::exception& e)
    {
        fileDlg.error = e.what();
    }

    if (ImGui::Selectable("..", false, ImGuiSelectableFlags_AllowDoubleClick))
    {
        auto parent = fileDlg.currentDir.parent_path();
        if (!parent.empty())
        {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                // Double-click: go up immediately
                fileDlg.currentDir    = parent;
                fileDlg.selectedEntry.clear();
            }
            else
            {
                fileDlg.currentDir    = parent;
                fileDlg.selectedEntry.clear();
            }
        }
    }

    for (const auto& entry : entries)
    {
        std::string name = entry.path().filename().string();
        bool isDir = entry.is_directory();

        // Filter out files whose extension is not in allowedExtensions
        if (!isDir)
        {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            bool allowed = fileDlg.allowedExtensions.empty(); // if none, show all
            if (!allowed)
            {
                for (auto a : fileDlg.allowedExtensions)
                {
                    std::transform(a.begin(), a.end(), a.begin(), ::tolower);
                    if (ext == a)
                    {
                        allowed = true;
                        break;
                    }
                }
            }

            if (!allowed)
                continue; // skip drawing this file
        }

        std::string label   = isDir ? (name + "/") : name;
        bool selected       = (fileDlg.selectedEntry == name);

        if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick))
        {
            // Always update selection on click
            fileDlg.selectedEntry = name;

            // If it was a double-click, "open" the item immediately
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                fs::path path = fileDlg.currentDir / name;
                openPath(path);
            }
        }
    }

    ImGui::EndChild();

    if (!fileDlg.error.empty())
    {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "%s", fileDlg.error.c_str());
    }

    // Bottom buttons
    if (ImGui::Button("Up"))
    {
        auto parent = fileDlg.currentDir.parent_path();
        if (!parent.empty())
        {
            fileDlg.currentDir    = parent;
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

    bool hasSelection = !fileDlg.selectedEntry.empty();
    if (!hasSelection)
        ImGui::BeginDisabled();

    if (ImGui::Button("Open"))
    {
        fs::path path = fileDlg.currentDir / fileDlg.selectedEntry;

        try
        {
            if (fs::is_directory(path))
            {
                fileDlg.currentDir    = path;
                fileDlg.selectedEntry.clear();
            }
            else
            {
                // Check extension against allowed list, if any
                std::string ext = path.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                bool allowed = fileDlg.allowedExtensions.empty();  // if none given, accept all
                if (!allowed)
                {
                    for (const auto& a : fileDlg.allowedExtensions)
                    {
                        if (ext == a)
                        {
                            allowed = true;
                            break;
                        }
                    }
                }

                if (!allowed)
                {
                    fileDlg.error = "File type not allowed for this action.";
                }
                else
                {
                    push(pendingType_, path.string());
                    fileDlg.open = false;
                    fileDlg.selectedEntry.clear();
                }
            }
        }
        catch (const std::exception& e)
        {
            fileDlg.error = e.what();
        }
    }

    if (!hasSelection)
        ImGui::EndDisabled();

    ImGui::End();
}

void EmulatorUI::installMenu(const MediaViewState& v)
{
    static bool aboutRequested = false;

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Attach D64/D71 image...", "Ctrl+D"))
                startFileDialog("Select D64/D71 Image", { ".d64", ".d71" }, UiCommand::Type::AttachDisk);

            if (ImGui::MenuItem("Attach PRG/P00 image...", "Ctrl+P"))
                startFileDialog("Select PRG/P00 Image", { ".prg", ".p00" }, UiCommand::Type::AttachPRG);

            if (ImGui::MenuItem("Attach Cartridge image...", "Ctrl+C"))
                startFileDialog("Select CRT Image", { ".crt" }, UiCommand::Type::AttachCRT);

            if (ImGui::MenuItem("Attach T64 image...", "Ctrl+T"))
                startFileDialog("Select T64 image", { ".t64" }, UiCommand::Type::AttachT64);

            if (ImGui::MenuItem("Attach TAP image...", "Ctrl+U"))
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

            if (ImGui::MenuItem("Clear Port 1 Pad")) push(UiCommand::Type::ClearPort1Pad);
            if (ImGui::MenuItem("Clear Port 2 Pad")) push(UiCommand::Type::ClearPort2Pad);
            if (ImGui::MenuItem("Swap Port 1/2 Pads")) push(UiCommand::Type::SwapPortPads);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("System"))
        {
            if (ImGui::MenuItem("Warm Reset", "Ctrl+W"))       push(UiCommand::Type::WarmReset);
            if (ImGui::MenuItem("Cold Reset", "Ctrl+Shift+R")) push(UiCommand::Type::ColdReset);

            bool isPAL = v.pal;
            if (ImGui::MenuItem("NTSC", nullptr, !isPAL)) push(UiCommand::Type::SetNTSC);
            if (ImGui::MenuItem("PAL",  nullptr,  isPAL)) push(UiCommand::Type::SetPAL);

            ImGui::Separator();

            bool paused = v.paused;
            if (ImGui::MenuItem(paused ? "Resume" : "Pause", "Space")) push(UiCommand::Type::TogglePause);

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
        ImGui::Text("F12 opens ML Monitor.\nAlt+J, 1/2 attach joysticks.");
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    drawFileDialog();
}
