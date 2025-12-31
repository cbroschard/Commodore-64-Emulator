// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"

MLMonitor::MLMonitor() :
    monbackend(nullptr),
    running(false)
{
    memset(InputBuf, 0, sizeof(InputBuf));

    // Register all commands
    registerCommand(std::make_unique<AssembleCommand>());
    registerCommand(std::make_unique<BreakpointCommand>());
    registerCommand(std::make_unique<CartridgeCommand>());
    registerCommand(std::make_unique<CIACommand>());
    registerCommand(std::make_unique<DisassembleCommand>());
    registerCommand(std::make_unique<DriveCommand>());
    registerCommand(std::make_unique<ExportDisassemblyCommand>());
    registerCommand(std::make_unique<GoCommand>());
    registerCommand(std::make_unique<IECCommand>());
    registerCommand(std::make_unique<IRQCommand>());
    registerCommand(std::make_unique<JamCommand>());
    registerCommand(std::make_unique<LogCommand>());
    registerCommand(std::make_unique<MemoryDumpCommand>());
    registerCommand(std::make_unique<MemoryEditCommand>());
    registerCommand(std::make_unique<MemoryEditDirectCommand>());
    registerCommand(std::make_unique<NextCommand>());
    registerCommand(std::make_unique<PLACommand>());
    registerCommand(std::make_unique<RegisterDumpCommand>());
    registerCommand(std::make_unique<ResetCommand>());
    registerCommand(std::make_unique<SIDCommand>());
    registerCommand(std::make_unique<StepCommand>());
    registerCommand(std::make_unique<TapeCommand>());
    registerCommand(std::make_unique<TraceCommand>());
    registerCommand(std::make_unique<VICCommand>());
    registerCommand(std::make_unique<WatchCommand>());

    addLog("Welcome to the Commodore 64 Monitor Debugger.");
    addLog("Type 'help' for available commands.");
}

MLMonitor::~MLMonitor() = default;

void MLMonitor::addLog(const char* fmt, ...)
{
    int old_size = Items.size();
    va_list args;
    va_start(args, fmt);
    Items.appendfv(fmt, args);
    va_end(args);
    Items.append("\n");

    // Update line offsets
    for (int new_size = Items.size(); old_size < new_size; old_size++)
        if (Items[old_size] == '\n')
            LineOffsets.push_back(old_size + 1);

    if (AutoScroll) ScrollToBottom = true;
}

void MLMonitor::draw(bool* p_open)
{
    ImGui::SetNextWindowSize(ImVec2(900, 550), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(60, 60), ImGuiCond_Appearing);

    if (!ImGui::Begin("ML Monitor", p_open))
    {
        ImGui::End();
        return;
    }

    // Ensure this window grabs focus when it appears (helps caret show up immediately)
    if (ImGui::IsWindowAppearing())
        ImGui::SetWindowFocus();

    // Options menu
    if (ImGui::BeginPopup("Options"))
    {
        ImGui::Checkbox("Auto-scroll", &AutoScroll);
        if (ImGui::Button("Clear")) { Items.clear(); LineOffsets.clear(); }
        ImGui::EndPopup();
    }
    if (ImGui::Button("Options")) ImGui::OpenPopup("Options");
    ImGui::SameLine();
    if (ImGui::Button("Clear")) { Items.clear(); LineOffsets.clear(); }

    ImGui::Separator();

    // Reserve space for a separator + InputText
    const float footer_height_to_reserve =
        ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();

    // Scrolling Region
    ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    if (ImGui::BeginPopupContextWindow())
    {
        if (ImGui::Selectable("Clear")) { Items.clear(); LineOffsets.clear(); }
        ImGui::EndPopup();
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1)); // Tight spacing
    const char* buf = Items.begin();
    const char* buf_end = Items.end();
    ImGui::TextUnformatted(buf, buf_end);

    if (ScrollToBottom || (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f))
        ImGui::SetScrollHereY(1.0f);
    ScrollToBottom = false;

    ImGui::PopStyleVar();
    ImGui::EndChild();

    // If user clicks the scrollback, bring focus back to input next frame
    if (ImGui::IsItemClicked())
        { static bool& f = *(new bool(false)); (void)f; } // <-- remove this line, see note below
    // (we'll set focus_input_next_frame below where it exists)

    ImGui::Separator();

    // --- Command line (always visible + obvious) ---
    static bool focus_input_next_frame = false;

    // If user clicked scrollback, refocus input
    if (ImGui::IsItemClicked())
        focus_input_next_frame = true;

    ImGui::Spacing();
    ImGui::TextDisabled("Command:");
    ImGui::SameLine();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(">");
    ImGui::SameLine();

    ImGui::SetNextItemWidth(-1.0f);

    ImGuiInputTextFlags input_flags =
        ImGuiInputTextFlags_EnterReturnsTrue |
        ImGuiInputTextFlags_CallbackCompletion |
        ImGuiInputTextFlags_CallbackHistory;

    if (ImGui::IsWindowAppearing() || focus_input_next_frame)
    {
        ImGui::SetKeyboardFocusHere();
        focus_input_next_frame = false;
    }

    if (ImGui::InputText("##Input", InputBuf, IM_ARRAYSIZE(InputBuf), input_flags,
        [](ImGuiInputTextCallbackData* data) { return 0; }))
    {
        char* s = InputBuf;
        if (s[0]) execCommand(s);
        strcpy(s, "");

        focus_input_next_frame = true;
    }

    ImGui::End();
}

void MLMonitor::captureOutputAndExecute(const std::string& cmdLine)
{
    const std::string out = executeAndCapture(cmdLine);
    if (!out.empty())
        addLog("%s", out.c_str());
}

std::string MLMonitor::executeAndCapture(const std::string& cmdLine)
{
    std::ostringstream buffer;
    auto* old = std::cout.rdbuf(buffer.rdbuf()); // Redirect std::cout to buffer

    try
    {
        handleCommand(cmdLine);
    }
    catch (const std::exception& ex)
    {
        std::cout << "Error executing command: " << ex.what() << "\n";
    }
    catch (...)
    {
        std::cout << "Error executing command: unknown exception\n";
    }

    std::cout.rdbuf(old); // Restore std::cout
    return buffer.str();
}

void MLMonitor::execCommand(const char* command_line)
{
    addLog("# %s\n", command_line);

    // Add to history
    HistoryPos = -1;
    for (int i = History.size() - 1; i >= 0; i--)
        if (History[i] == command_line) { History.erase(History.begin() + i); break; }
    History.push_back(command_line);

    captureOutputAndExecute(command_line);
}

void MLMonitor::enter()
{
    running = true;
    std::string line;
    while (running)
    {
        std::cout << "monitor> ";
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;
        handleCommand(line);
    }
}

void MLMonitor::attachTraceManagerInstance(TraceManager* tm)
{
    auto it = commands.find("trace");
    if (it != commands.end()) {
        if (auto* tc = dynamic_cast<TraceCommand*>(it->second.get()))
        {
            tc->attachTraceManagerInstance(tm);
        }
    }
}

void MLMonitor::clearBreakpoint(uint16_t bp)
{
    auto record = breakpoints.find(bp);
    if (record != breakpoints.end())
    {
        breakpoints.erase(record);
    }
}

void MLMonitor::listBreakpoints() const
{
    int index = 0;
    for (auto list : breakpoints)
    {
        std::cout << "[" << index << "]" << "  $" << std::hex << std::setw(4) << std::setfill('0') << list << std::endl;
        index++;
    }
}

void MLMonitor::addWriteWatch(uint16_t address)
{
    uint8_t value = monbackend->readRAM(address);
    writeWatches[address] = value;
    std::cout << "Watchpoint set at $" << std::hex << std::setw(4) << std::setfill('0') << address
              << " (initial value = $" << std::setw(2) << static_cast<int>(value) << ")\n";
}

void MLMonitor::clearWriteWatch(uint16_t address)
{
    if (writeWatches.erase(address))
    {
        std::cout << "Watchpoint cleared at $" << std::hex << std::setw(4) << std::setfill('0') << address << "\n";
    }
    else
    {
        std::cout << "No watchpoint found at $" << std::hex << std::setw(4) << std::setfill('0') << address << "\n";
    }
}

void MLMonitor::clearAllWriteWatches()
{
    for (auto it = writeWatches.begin(); it != writeWatches.end(); )
    {
        uint16_t address = it->first;
        std::cout << "Watchpoint cleared at $"
                  << std::hex << std::setw(4) << std::setfill('0')
                  << address << "\n";

        it = writeWatches.erase(it); // erase returns next valid iterator
    }

    std::cout << "All writeWatches cleared.\n";
}

void MLMonitor::listWriteWatches() const
{
    int index = 0;
    for (const auto& [address, value] : writeWatches)
    {
        std::cout << "[" << index << "]  $" << std::hex << std::setw(4) << std::setfill('0') << address
                  << " (last value=$" << std::setw(2) << static_cast<int>(value) << ")\n";
        index++;
    }
}

bool MLMonitor::checkWatchWrite(uint16_t address, uint8_t newVal)
{
    auto it = writeWatches.find(address);
    if (it != writeWatches.end())
    {
        if (it->second != newVal)
        {
            uint8_t oldVal = it->second;
            it->second = newVal;

            std::cout << ">>> Watchpoint hit at $" << std::hex << std::setw(4) << std::setfill('0') << address
                      << ": old=$" << std::setw(2) << static_cast<int>(oldVal)
                      << " new=$" << std::setw(2) << static_cast<int>(newVal) << "\n";
            return true; // signal to break execution
        }
    }
    return false;
}

void MLMonitor::addReadWatch(uint16_t address)
{
    readWatches.insert(address);
    std::cout << "Read watchpoint set at $" << std::hex << std::setw(4) << std::setfill('0') << address << "\n";
}

void MLMonitor::clearReadWatch(uint16_t address)
{
    if (readWatches.erase(address))
        std::cout << "Read watchpoint cleared at $" << std::hex << std::setw(4) << std::setfill('0') << address << "\n";
    else
        std::cout << "No read watchpoint found at $" << std::hex << std::setw(4) << std::setfill('0') << address << "\n";
}

void MLMonitor::clearAllReadWatches()
{
    for (auto it = readWatches.begin(); it != readWatches.end(); )
    {
        uint16_t addr = *it;
        std::cout << "Read watchpoint cleared at $" << std::hex << std::setw(4) << std::setfill('0') << addr << "\n";
        it = readWatches.erase(it);
    }
    std::cout << "All read watchpoints cleared.\n";
}

std::vector<uint16_t> MLMonitor::getWriteWatchAddresses() const
{
    std::vector<uint16_t> out;
    out.reserve(writeWatches.size());
    for (const auto& kv : writeWatches) out.push_back(kv.first);
    return out;
}

void MLMonitor::listReadWatches() const
{
    int i = 0;
    for (auto addr : readWatches)
    {
        std::cout << "[" << i++ << "]  $" << std::hex << std::setw(4) << std::setfill('0') << addr << "\n";
    }
}

bool MLMonitor::checkWatchRead(uint16_t address, uint8_t value)
{
    if (readWatches.find(address) != readWatches.end())
    {
        std::cout << ">>> Read watchpoint hit at $"
                  << std::hex << std::setw(4) << std::setfill('0') << address
                  << " (value=$" << std::setw(2) << static_cast<int>(value) << ")\n";
        return true; // tell the caller to break execution
    }
    return false;
}

std::vector<uint16_t> MLMonitor::getReadWatchAddresses() const
{
    return std::vector<uint16_t>(readWatches.begin(), readWatches.end());
}

bool MLMonitor::isRasterWaitLoop(uint16_t pc, uint8_t& targetRaster)
{
    uint8_t opcode = monbackend->getOpCode(pc);

    // Case 1: CMP #imm / BNE (or BEQ) after LDA $D012
    if (opcode == 0xD0 || opcode == 0xF0)  // BNE or BEQ
    {
        uint8_t ldaOp = monbackend->getOpCode(pc - 5);
        uint8_t ldaLo = monbackend->getOpCode(pc - 4);
        uint8_t ldaHi = monbackend->getOpCode(pc - 3);
        uint8_t cmpOp = monbackend->getOpCode(pc - 2);
        uint8_t imm   = monbackend->getOpCode(pc - 1);

        if (ldaOp == 0xAD && ldaLo == 0x12 && ldaHi == 0xD0 && cmpOp == 0xC9)
        {
            targetRaster = imm;
            return true;
        }
    }

    // Case 2: CPY $D012 / BNE (or BEQ) after LDY #imm
    if (opcode == 0xD0 || opcode == 0xF0)  // BNE or BEQ
    {
        uint8_t ldyOp = monbackend->getOpCode(pc - 3);
        uint8_t imm   = monbackend->getOpCode(pc - 2);
        uint8_t cpyOp = monbackend->getOpCode(pc - 1);
        uint8_t cpyLo = monbackend->getOpCode(pc - 0); // careful: overlaps PC

        // Actually: LDY #imm (A0 xx), CPY $D012 (CC 12 D0), BNE/BEQ
        if (ldyOp == 0xA0 && cpyOp == 0xCC && cpyLo == 0x12)
        {
            uint8_t cpyHi = monbackend->getOpCode(pc + 1); // hi byte after $12
            if (cpyHi == 0xD0)
            {
                targetRaster = imm;
                return true;
            }
        }
    }

    return false;
}

void MLMonitor::handleCommand(const std::string& line)
{
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;

    // Normalize to lowercase
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

    if (cmd == "exit" || cmd == "q" || cmd == "quit")
    {
        running = false;
        return;
    }

    if (cmd == "help" || cmd == "h" || cmd == "?")
    {
        // If user typed: help <command>
        std::string topic;
        if (iss >> topic)
        {
            std::transform(topic.begin(), topic.end(), topic.begin(), ::tolower);

            auto it = commands.find(topic);
            if (it != commands.end())
            {
                const std::string txt = it->second->help();
                std::cout << txt;
                if (!txt.empty() && txt.back() != '\n')
                    std::cout << "\n";
            }
            else
            {
                std::cout << "Unknown command: " << topic << "\n";
            }
            return;
        }

        // Plain "help" => main help
        std::map<std::string, std::vector<std::string>> grouped;
        for (const auto& kv : commands)
            grouped[kv.second->category()].push_back(kv.second->shortHelp());

        std::cout << "Available commands:\n";
        for (auto& [cat, cmds] : grouped)
        {
            std::cout << "  " << cat << ":\n";
            for (auto& line : cmds)
                std::cout << "    " << line << "\n";
        }
        return;
    }

    std::vector<std::string> args;
    args.push_back(cmd);
    std::string token;
    while (iss >> token) args.push_back(token);

    auto it = commands.find(cmd);
    if (it != commands.end()) it->second->execute(*this, args);
    else std::cout << "Unknown command: " << cmd << "\n";
}

void MLMonitor::registerCommand(std::unique_ptr<MonitorCommand> cmd)
{
    commands[cmd->name()] = std::move(cmd);
}
