// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/MLMonitor.h"
#include "Debug/TraceCommand.h"

TraceCommand::TraceCommand() :
    traceMgr(nullptr)
{

}

TraceCommand::~TraceCommand() = default;

int TraceCommand::order() const
{
    return 30;
}

std::string TraceCommand::name() const
{
    return "trace";
}

std::string TraceCommand::category() const
{
    return "CPU/Execution";
}

std::string TraceCommand::shortHelp() const
{
    return "trace     - Enable or control component Tracing";
}

std::string TraceCommand::help() const
{
    return
        "Trace - control tracing output and categories\n"
        "\n"
        "Usage:\n"
        "  trace                            Show global trace status (ON/OFF)\n"
        "  trace on|off                     Enable or disable tracing globally\n"
        "  trace cats|categories            List all top-level chip categories and their status\n"
        "  trace details                    List all CPU/VIC/CIA/PLA/Memory detail categories and their status\n"
        "  trace dump                       Dump the current trace buffer to console\n"
        "  trace clear                      Clear stored trace data\n"
        "  trace file <path>                Write trace output to a file\n"
        "  trace status                     Show global, category, and detail trace status\n"
        "  trace all enable|disable         Enable or disable all trace categories and details\n"
        "\n"
        "Cartridge tracing:\n"
        "  trace cart enable                Enable Cartridge tracing\n"
        "  trace cart disable               Disable Cartridge tracing\n"
        "\n"
        "CIA tracing:\n"
        "  trace cia1 enable                 Enable CIA1 top-level tracing\n"
        "  trace cia1 disable                Disable CIA1 top-level tracing\n"
        "  trace cia1 all enable|disable     Enable or disable CIA1 detail tracing\n"
        "  trace cia1 timer enable|disable   CIA1 timer tracing\n"
        "  trace cia1 irq enable|disable     CIA1 interrupt tracing\n"
        "  trace cia1 cnt enable|disable     CIA1 CNT-edge tracing\n"
        "  trace cia2 enable                 Enable CIA2 top-level tracing\n"
        "  trace cia2 disable                Disable CIA2 top-level tracing\n"
        "  trace cia2 all enable|disable     Enable or disable CIA2 detail tracing\n"
        "  trace cia2 timer enable|disable   CIA2 timer tracing\n"
        "  trace cia2 irq enable|disable     CIA2 interrupt tracing\n"
        "  trace cia2 cnt enable|disable     CIA2 CNT-edge tracing\n"
        "  trace cia2 iec enable|disable     CIA2 IEC bus tracing\n"
        "\n"
        "CPU tracing:\n"
        "  trace cpu enable                 Enable CPU top-level tracing\n"
        "  trace cpu disable                Disable CPU top-level tracing\n"
        "  trace cpu all enable|disable     Enable or disable all CPU detail tracing\n"
        "  trace cpu exec enable|disable    CPU instruction execution tracing\n"
        "  trace cpu irq enable|disable     CPU IRQ tracing\n"
        "  trace cpu nmi enable|disable     CPU NMI tracing\n"
        "  trace cpu stack enable|disable   CPU stack tracing\n"
        "  trace cpu branch enable|disable  CPU branch tracing\n"
        "  trace cpu flags enable|disable   CPU flags tracing\n"
        "  trace cpu ba enable|disable      CPU BA hold tracing\n"
        "  trace cpu jam enable|disable     CPU JAM/halt tracing\n"
        "\n"
        "Memory tracing:\n"
        "  trace mem enable                 Enable Memory top-level tracing\n"
        "  trace mem disable                Disable Memory top-level tracing\n"
        "  trace mem all enable|disable     Enable or disable all Memory detail tracing\n"
        "  trace mem cpu enable|disable     CPU read/write memory tracing\n"
        "  trace mem io enable|disable      I/O dispatch tracing\n"
        "  trace mem cart enable|disable    Cartridge/overlay memory tracing\n"
        "  trace mem port enable|disable    $0000/$0001 and port-side-effect tracing\n"
        "  trace mem add <lo>-<hi>          Add a traced address range (hex, inclusive)\n"
        "  trace mem list                   List currently traced memory ranges\n"
        "  trace mem clear                  Clear all traced memory ranges\n"
        "\n"
        "PLA tracing:\n"
        "  trace pla enable                 Enable PLA top-level tracing\n"
        "  trace pla disable                Disable PLA top-level tracing\n"
        "  trace pla all enable|disable     Enable or disable all PLA detail tracing\n"
        "  trace pla mode enable|disable    PLA mode/line-change tracing\n"
        "  trace pla port enable|disable    PLA $0001 port-write tracing\n"
        "  trace pla resolve enable|disable PLA address-resolution tracing\n"
        "\n"
        "SID tracing:\n"
        "  trace sid enable                 Enable SID tracing\n"
        "  trace sid disable                Disable SID tracing\n"
        "\n"
        "VIC tracing:\n"
        "  trace vic enable                 Enable VIC top-level tracing\n"
        "  trace vic disable                Disable VIC top-level tracing\n"
        "  trace vic all enable|disable     Enable or disable all VIC detail tracing\n"
        "  trace vic raster enable|disable  VIC raster tracing\n"
        "  trace vic irq enable|disable     VIC IRQ tracing\n"
        "  trace vic reg enable|disable     VIC register tracing\n"
        "  trace vic badline enable|disable VIC badline tracing\n"
        "  trace vic sprite enable|disable  VIC sprite tracing\n"
        "  trace vic bus enable|disable     VIC bus/AEC/BA tracing\n"
        "  trace vic event enable|disable   VIC general event tracing\n"
        "\n"
        "Notes:\n"
        "  - Addresses use $HHHH hex notation, e.g. $0800-$0FFF.\n"
        "  - 'trace mem add' does NOT enable the MEM category; use 'trace mem enable'.\n"
        "  - Global tracing must be ON for output: use 'trace on'.\n"
        "  - Enabling a CPU/VIC detail also enables that chip's top-level category.\n"
        "\n"
        "Examples:\n"
        "  trace on\n"
        "  trace status\n"
        "  trace details\n"
        "  trace file traces.txt\n"
        "  trace cpu irq enable\n"
        "  trace cpu all enable\n"
        "  trace vic raster enable\n"
        "  trace vic sprite enable\n"
        "  trace mem add $0800-$0FFF\n"
        "  trace mem enable\n"
        "  trace dump\n";
}

void TraceCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    (void)mon;

    if (!traceMgr)
    {
        std::cout << "Trace manager not available.\n";
        return;
    }

    auto parseHexRange = [&](const std::string& s, uint16_t& lo, uint16_t& hi) -> bool
    {
        const size_t dash = s.find('-');
        if (dash == std::string::npos)
            return false;

        auto parseOne = [](std::string part, uint16_t& out) -> bool
        {
            if (!part.empty() && part[0] == '$')
                part.erase(part.begin());

            if (part.size() < 1 || part.size() > 4)
                return false;

            unsigned value = 0;
            std::istringstream iss(part);
            iss >> std::hex >> value;

            if (iss.fail() || !iss.eof() || value > 0xFFFF)
                return false;

            out = static_cast<uint16_t>(value);
            return true;
        };

        uint16_t a = 0, b = 0;
        if (!parseOne(s.substr(0, dash), a))
            return false;
        if (!parseOne(s.substr(dash + 1), b))
            return false;
        if (a > b)
            return false;

        lo = a;
        hi = b;
        return true;
    };

    auto tracingOnReminder = [&]()
    {
        if (!traceMgr->isEnabled())
            std::cout << "Tracing is not turned on. Run: trace on\n";
    };

    auto disableGlobalReminder = [&]()
    {
        if (traceMgr->isEnabled())
            std::cout << "Tracing is still enabled globally. To disable it, run: trace off\n";
    };

    auto isEnableWord = [](const std::string& s) -> bool
    {
        return s == "enable" || s == "on";
    };

    auto isDisableWord = [](const std::string& s) -> bool
    {
        return s == "disable" || s == "off";
    };

    auto setChipCategory = [&](TraceManager::TraceCat cat, const char* label, const std::string& action) -> bool
    {
        if (isEnableWord(action))
        {
            traceMgr->enableCategory(cat);
            std::cout << "Enabled " << label << " tracing.\n";
            tracingOnReminder();
            return true;
        }
        if (isDisableWord(action))
        {
            traceMgr->disableCategory(cat);
            std::cout << "Disabled " << label << " tracing.\n";
            disableGlobalReminder();
            return true;
        }
        return false;
    };

    auto setCiaDetail = [&](TraceManager::TraceCat cat, TraceManager::TraceDetail detail, const char* chipLabel,
            const char* detailLabel, const std::string& action) -> bool
    {
        if (isEnableWord(action))
        {
            traceMgr->enableCategory(cat);
            traceMgr->enableDetail(detail);
            std::cout << "Enabled " << chipLabel << " " << detailLabel << " tracing.\n";
            tracingOnReminder();
            return true;
        }
        if (isDisableWord(action))
        {
            traceMgr->disableDetail(detail);
            std::cout << "Disabled " << chipLabel << " " << detailLabel << " tracing.\n";
            disableGlobalReminder();
            return true;
        }
        return false;
    };

    auto setCpuDetail = [&](TraceManager::TraceDetail detail, const char* label, const std::string& action) -> bool
    {
        if (isEnableWord(action))
        {
            traceMgr->enableCategory(TraceManager::TraceCat::CPU);
            traceMgr->enableDetail(detail);
            std::cout << "Enabled CPU " << label << " tracing.\n";
            tracingOnReminder();
            return true;
        }
        if (isDisableWord(action))
        {
            traceMgr->disableDetail(detail);
            std::cout << "Disabled CPU " << label << " tracing.\n";
            disableGlobalReminder();
            return true;
        }
        return false;
    };

    auto setMemDetail = [&](TraceManager::TraceDetail detail, const char* label, const std::string& action) -> bool
    {
        if (isEnableWord(action))
        {
            traceMgr->enableCategory(TraceManager::TraceCat::MEM);
            traceMgr->enableDetail(detail);
            std::cout << "Enabled Memory " << label << " tracing.\n";
            tracingOnReminder();
            return true;
        }
        if (isDisableWord(action))
        {
            traceMgr->disableDetail(detail);
            std::cout << "Disabled Memory " << label << " tracing.\n";
            disableGlobalReminder();
            return true;
        }
        return false;
    };

    auto setPlaDetail = [&](TraceManager::TraceDetail detail, const char* label, const std::string& action) -> bool
    {
        if (isEnableWord(action))
        {
            traceMgr->enableCategory(TraceManager::TraceCat::PLA);
            traceMgr->enableDetail(detail);
            std::cout << "Enabled PLA " << label << " tracing.\n";
            tracingOnReminder();
            return true;
        }
        if (isDisableWord(action))
        {
            traceMgr->disableDetail(detail);
            std::cout << "Disabled PLA " << label << " tracing.\n";
            disableGlobalReminder();
            return true;
        }
        return false;
    };

    auto setVicDetail = [&](TraceManager::TraceDetail detail, const char* label, const std::string& action) -> bool
    {
        if (isEnableWord(action))
        {
            traceMgr->enableCategory(TraceManager::TraceCat::VIC);
            traceMgr->enableDetail(detail);
            std::cout << "Enabled VIC " << label << " tracing.\n";
            tracingOnReminder();
            return true;
        }
        if (isDisableWord(action))
        {
            traceMgr->disableDetail(detail);
            std::cout << "Disabled VIC " << label << " tracing.\n";
            disableGlobalReminder();
            return true;
        }
        return false;
    };

    // No subcommand => show status
    if (args.size() == 1)
    {
        std::cout << "Trace " << (traceMgr->isEnabled() ? "ON" : "OFF") << "\n";
        return;
    }

    const std::string& sub = args[1];

    if (isHelp(sub))
    {
        std::cout << help();
        return;
    }

    if (sub == "on")
    {
        traceMgr->enable(true);
        std::cout << "Tracing enabled.\n";
        return;
    }

    if (sub == "off")
    {
        traceMgr->enable(false);
        std::cout << "Tracing disabled.\n";
        return;
    }

    if (sub == "cats" || sub == "categories")
    {
        std::cout << traceMgr->listCategoryStatus() << "\n";
        return;
    }

    if (sub == "details")
    {
        std::cout << traceMgr->listDetailStatus() << "\n";
        return;
    }

    if (sub == "dump")
    {
        traceMgr->dumpBuffer();
        return;
    }

    if (sub == "clear")
    {
        traceMgr->clearBuffer();
        std::cout << "Trace buffer cleared.\n";
        return;
    }

    if (sub == "file")
    {
        if (args.size() < 3)
        {
            std::cout << "Usage: trace file <path>\n";
            return;
        }

        const std::string path = joinArgs(args, 2);
        traceMgr->setFileOutput(path);
        std::cout << "Trace file set to " << path << "\n";
        return;
    }

    if (sub == "status")
    {
        std::cout << "Trace " << (traceMgr->isEnabled() ? "ON" : "OFF") << "\n";
        std::cout << traceMgr->listCategoryStatus() << "\n";
        std::cout << traceMgr->listDetailStatus() << "\n";
        return;
    }

    if (sub == "all")
    {
        if (args.size() >= 3 && isEnableWord(args[2]))
        {
            traceMgr->enableAllCategories(true);
            traceMgr->enableAllDetails(true);
            std::cout << "Enabled all trace categories and details.\n";
            tracingOnReminder();
            return;
        }
        if (args.size() >= 3 && isDisableWord(args[2]))
        {
            traceMgr->enableAllDetails(false);
            traceMgr->enableAllCategories(false);
            std::cout << "Disabled all trace categories and details.\n";
            disableGlobalReminder();
            return;
        }

        std::cout << "Usage: trace all enable|disable\n";
        return;
    }

    if (sub == "cart")
    {
        if (args.size() >= 3 && setChipCategory(TraceManager::TraceCat::CART, "Cartridge", args[2]))
            return;

        std::cout << "Usage: trace cart enable|disable\n";
        return;
    }

    if (sub == "cia1")
    {
        if (args.size() >= 3 && setChipCategory(TraceManager::TraceCat::CIA1, "CIA1", args[2]))
            return;

        if (args.size() >= 4 && args[2] == "all")
        {
            if (isEnableWord(args[3]))
            {
                traceMgr->enableCategory(TraceManager::TraceCat::CIA1);
                traceMgr->enableDetail(TraceManager::TraceDetail::CIA_TIMER);
                traceMgr->enableDetail(TraceManager::TraceDetail::CIA_IRQ);
                traceMgr->enableDetail(TraceManager::TraceDetail::CIA_CNT);
                std::cout << "Enabled all CIA1 trace details.\n";
                tracingOnReminder();
                return;
            }
            if (isDisableWord(args[3]))
            {
                traceMgr->disableDetail(TraceManager::TraceDetail::CIA_TIMER);
                traceMgr->disableDetail(TraceManager::TraceDetail::CIA_IRQ);
                traceMgr->disableDetail(TraceManager::TraceDetail::CIA_CNT);
                std::cout << "Disabled all CIA1 trace details.\n";
                disableGlobalReminder();
                return;
            }

            std::cout << "Usage: trace cia1 all enable|disable\n";
            return;
        }

        if (args.size() >= 4)
        {
            const std::string& detail = args[2];
            const std::string& action = args[3];

            if (detail == "timer" && setCiaDetail(TraceManager::TraceCat::CIA1, TraceManager::TraceDetail::CIA_TIMER, "CIA1", "timer", action)) return;
            if (detail == "irq"   && setCiaDetail(TraceManager::TraceCat::CIA1, TraceManager::TraceDetail::CIA_IRQ,   "CIA1", "irq",   action)) return;
            if (detail == "cnt"   && setCiaDetail(TraceManager::TraceCat::CIA1, TraceManager::TraceDetail::CIA_CNT,   "CIA1", "cnt",   action)) return;

            std::cout << "Usage: trace cia1 <timer|irq|cnt> enable|disable\n";
            return;
        }

        std::cout << "Usage: trace cia1 enable|disable\n"
                     "       trace cia1 all enable|disable\n"
                     "       trace cia1 <timer|irq|cnt> enable|disable\n";
        return;
    }

    if (sub == "cia2")
    {
        if (args.size() >= 3 && setChipCategory(TraceManager::TraceCat::CIA2, "CIA2", args[2]))
            return;

        if (args.size() >= 4 && args[2] == "all")
        {
            if (isEnableWord(args[3]))
            {
                traceMgr->enableCategory(TraceManager::TraceCat::CIA2);
                traceMgr->enableDetail(TraceManager::TraceDetail::CIA_TIMER);
                traceMgr->enableDetail(TraceManager::TraceDetail::CIA_IRQ);
                traceMgr->enableDetail(TraceManager::TraceDetail::CIA_CNT);
                traceMgr->enableDetail(TraceManager::TraceDetail::CIA_IEC);
                std::cout << "Enabled all CIA2 trace details.\n";
                tracingOnReminder();
                return;
            }
            if (isDisableWord(args[3]))
            {
                traceMgr->disableDetail(TraceManager::TraceDetail::CIA_TIMER);
                traceMgr->disableDetail(TraceManager::TraceDetail::CIA_IRQ);
                traceMgr->disableDetail(TraceManager::TraceDetail::CIA_CNT);
                traceMgr->disableDetail(TraceManager::TraceDetail::CIA_IEC);
                std::cout << "Disabled all CIA2 trace details.\n";
                disableGlobalReminder();
                return;
            }

            std::cout << "Usage: trace cia2 all enable|disable\n";
            return;
        }

        if (args.size() >= 4)
        {
            const std::string& detail = args[2];
            const std::string& action = args[3];

            if (detail == "timer" && setCiaDetail(TraceManager::TraceCat::CIA2, TraceManager::TraceDetail::CIA_TIMER, "CIA2", "timer", action)) return;
            if (detail == "irq"   && setCiaDetail(TraceManager::TraceCat::CIA2, TraceManager::TraceDetail::CIA_IRQ,   "CIA2", "irq",   action)) return;
            if (detail == "cnt"   && setCiaDetail(TraceManager::TraceCat::CIA2, TraceManager::TraceDetail::CIA_CNT,   "CIA2", "cnt",   action)) return;
            if (detail == "iec"   && setCiaDetail(TraceManager::TraceCat::CIA2, TraceManager::TraceDetail::CIA_IEC,   "CIA2", "iec",   action)) return;

            std::cout << "Usage: trace cia2 <timer|irq|cnt|iec> enable|disable\n";
            return;
        }

        std::cout << "Usage: trace cia2 enable|disable\n"
                     "       trace cia2 all enable|disable\n"
                     "       trace cia2 <timer|irq|cnt|iec> enable|disable\n";
        return;
    }

    if (sub == "cpu")
    {
        if (args.size() >= 3 && setChipCategory(TraceManager::TraceCat::CPU, "CPU", args[2]))
            return;

        if (args.size() >= 4 && args[2] == "all")
        {
            if (isEnableWord(args[3]))
            {
                traceMgr->enableCategory(TraceManager::TraceCat::CPU);
                traceMgr->enableCPUDetails(true);
                std::cout << "Enabled all CPU trace details.\n";
                tracingOnReminder();
                return;
            }
            if (isDisableWord(args[3]))
            {
                traceMgr->enableCPUDetails(false);
                std::cout << "Disabled all CPU trace details.\n";
                disableGlobalReminder();
                return;
            }

            std::cout << "Usage: trace cpu all enable|disable\n";
            return;
        }

        if (args.size() >= 4)
        {
            const std::string& detail = args[2];
            const std::string& action = args[3];

            if (detail == "exec"   && setCpuDetail(TraceManager::TraceDetail::CPU_EXEC,   "exec",   action)) return;
            if (detail == "irq"    && setCpuDetail(TraceManager::TraceDetail::CPU_IRQ,    "irq",    action)) return;
            if (detail == "nmi"    && setCpuDetail(TraceManager::TraceDetail::CPU_NMI,    "nmi",    action)) return;
            if (detail == "stack"  && setCpuDetail(TraceManager::TraceDetail::CPU_STACK,  "stack",  action)) return;
            if (detail == "branch" && setCpuDetail(TraceManager::TraceDetail::CPU_BRANCH, "branch", action)) return;
            if (detail == "flags"  && setCpuDetail(TraceManager::TraceDetail::CPU_FLAGS,  "flags",  action)) return;
            if (detail == "ba"     && setCpuDetail(TraceManager::TraceDetail::CPU_BA,     "ba",     action)) return;
            if (detail == "jam"    && setCpuDetail(TraceManager::TraceDetail::CPU_JAM,    "jam",    action)) return;

            std::cout << "Usage: trace cpu <exec|irq|nmi|stack|branch|flags|ba|jam> enable|disable\n";
            return;
        }

        std::cout << "Usage: trace cpu enable|disable\n"
                     "       trace cpu all enable|disable\n"
                     "       trace cpu <exec|irq|nmi|stack|branch|flags|ba|jam> enable|disable\n";
        return;
    }

    if (sub == "mem")
    {
        if (args.size() >= 3 && setChipCategory(TraceManager::TraceCat::MEM, "Memory", args[2]))
            return;

        if (args.size() >= 4 && args[2] == "all")
        {
            if (isEnableWord(args[3]))
            {
                traceMgr->enableCategory(TraceManager::TraceCat::MEM);
                traceMgr->enableDetail(TraceManager::TraceDetail::MEM_CPU);
                traceMgr->enableDetail(TraceManager::TraceDetail::MEM_IO);
                traceMgr->enableDetail(TraceManager::TraceDetail::MEM_CART);
                traceMgr->enableDetail(TraceManager::TraceDetail::MEM_PORT);
                std::cout << "Enabled all Memory trace details.\n";
                tracingOnReminder();
                return;
            }
            if (isDisableWord(args[3]))
            {
                traceMgr->disableDetail(TraceManager::TraceDetail::MEM_CPU);
                traceMgr->disableDetail(TraceManager::TraceDetail::MEM_IO);
                traceMgr->disableDetail(TraceManager::TraceDetail::MEM_CART);
                traceMgr->disableDetail(TraceManager::TraceDetail::MEM_PORT);
                std::cout << "Disabled all Memory trace details.\n";
                disableGlobalReminder();
                return;
            }

            std::cout << "Usage: trace mem all enable|disable\n";
            return;
        }

        if (args.size() >= 4)
        {
            const std::string& detail = args[2];
            const std::string& action = args[3];

            if (detail == "cpu"  && setMemDetail(TraceManager::TraceDetail::MEM_CPU,  "cpu",  action)) return;
            if (detail == "io"   && setMemDetail(TraceManager::TraceDetail::MEM_IO,   "io",   action)) return;
            if (detail == "cart" && setMemDetail(TraceManager::TraceDetail::MEM_CART, "cart", action)) return;
            if (detail == "port" && setMemDetail(TraceManager::TraceDetail::MEM_PORT, "port", action)) return;
        }

        if (args.size() >= 3 && args[2] == "add")
        {
            if (args.size() < 4)
            {
                std::cout << "Usage: trace mem add <lo>-<hi>\n";
                return;
            }

            uint16_t lo = 0, hi = 0;
            if (!parseHexRange(args[3], lo, hi))
            {
                std::cout << "Invalid range. Use $HHHH-$HHHH\n";
                return;
            }

            traceMgr->addMemRange(lo, hi);
            std::cout << "Added memory trace range $" << toHex(lo, 4)
                      << "-$" << toHex(hi, 4) << "\n";
            return;
        }

        if (args.size() >= 3 && args[2] == "list")
        {
            std::string ranges = traceMgr->listMemRange();
            if (ranges.empty())
                std::cout << "No memory trace ranges.\n";
            else
                std::cout << ranges << "\n";
            return;
        }

        if (args.size() >= 3 && args[2] == "clear")
        {
            traceMgr->clearMemRanges();
            std::cout << "Cleared memory trace ranges.\n";
            return;
        }

        std::cout << "Usage: trace mem enable|disable\n"
                     "       trace mem all enable|disable\n"
                     "       trace mem <cpu|io|cart|port> enable|disable\n"
                     "       trace mem add <lo>-<hi>\n"
                     "       trace mem list\n"
                     "       trace mem clear\n";
        return;
    }

    if (sub == "pla")
    {
        if (args.size() >= 3 && setChipCategory(TraceManager::TraceCat::PLA, "PLA", args[2]))
            return;

        if (args.size() >= 4 && args[2] == "all")
        {
            if (isEnableWord(args[3]))
            {
                traceMgr->enableCategory(TraceManager::TraceCat::PLA);
                traceMgr->enableDetail(TraceManager::TraceDetail::PLA_MODE);
                traceMgr->enableDetail(TraceManager::TraceDetail::PLA_PORT);
                traceMgr->enableDetail(TraceManager::TraceDetail::PLA_RESOLVE);
                std::cout << "Enabled all PLA trace details.\n";
                tracingOnReminder();
                return;
            }
            if (isDisableWord(args[3]))
            {
                traceMgr->disableDetail(TraceManager::TraceDetail::PLA_MODE);
                traceMgr->disableDetail(TraceManager::TraceDetail::PLA_PORT);
                traceMgr->disableDetail(TraceManager::TraceDetail::PLA_RESOLVE);
                std::cout << "Disabled all PLA trace details.\n";
                disableGlobalReminder();
                return;
            }

            std::cout << "Usage: trace pla all enable|disable\n";
            return;
        }

        if (args.size() >= 4)
        {
            const std::string& detail = args[2];
            const std::string& action = args[3];

            if (detail == "mode"    && setPlaDetail(TraceManager::TraceDetail::PLA_MODE,    "mode",    action)) return;
            if (detail == "port"    && setPlaDetail(TraceManager::TraceDetail::PLA_PORT,    "port",    action)) return;
            if (detail == "resolve" && setPlaDetail(TraceManager::TraceDetail::PLA_RESOLVE, "resolve", action)) return;

            std::cout << "Usage: trace pla <mode|port|resolve> enable|disable\n";
            return;
        }

        std::cout << "Usage: trace pla enable|disable\n"
                     "       trace pla all enable|disable\n"
                     "       trace pla <mode|port|resolve> enable|disable\n";
        return;
    }

    if (sub == "sid")
    {
        if (args.size() >= 3 && setChipCategory(TraceManager::TraceCat::SID, "SID", args[2]))
            return;

        std::cout << "Usage: trace sid enable|disable\n";
        return;
    }

    if (sub == "vic")
    {
        if (args.size() >= 3 && setChipCategory(TraceManager::TraceCat::VIC, "VIC", args[2]))
            return;

        if (args.size() >= 4 && args[2] == "all")
        {
            if (isEnableWord(args[3]))
            {
                traceMgr->enableCategory(TraceManager::TraceCat::VIC);
                traceMgr->enableVICDetails(true);
                std::cout << "Enabled all VIC trace details.\n";
                tracingOnReminder();
                return;
            }
            if (isDisableWord(args[3]))
            {
                traceMgr->enableVICDetails(false);
                std::cout << "Disabled all VIC trace details.\n";
                disableGlobalReminder();
                return;
            }

            std::cout << "Usage: trace vic all enable|disable\n";
            return;
        }

        if (args.size() >= 4)
        {
            const std::string& detail = args[2];
            const std::string& action = args[3];

            if (detail == "raster"  && setVicDetail(TraceManager::TraceDetail::VIC_RASTER,  "raster",  action)) return;
            if (detail == "irq"     && setVicDetail(TraceManager::TraceDetail::VIC_IRQ,     "irq",     action)) return;
            if (detail == "reg"     && setVicDetail(TraceManager::TraceDetail::VIC_REG,     "reg",     action)) return;
            if (detail == "badline" && setVicDetail(TraceManager::TraceDetail::VIC_BADLINE, "badline", action)) return;
            if (detail == "sprite"  && setVicDetail(TraceManager::TraceDetail::VIC_SPRITE,  "sprite",  action)) return;
            if (detail == "bus"     && setVicDetail(TraceManager::TraceDetail::VIC_BUS,     "bus",     action)) return;
            if (detail == "event"   && setVicDetail(TraceManager::TraceDetail::VIC_EVENT,   "event",   action)) return;

            std::cout << "Usage: trace vic <raster|irq|reg|badline|sprite|bus|event> enable|disable\n";
            return;
        }

        std::cout << "Usage: trace vic enable|disable\n"
                     "       trace vic all enable|disable\n"
                     "       trace vic <raster|irq|reg|badline|sprite|bus|event> enable|disable\n";
        return;
    }

    // Unknown subcommand
    std::cout << help();
}

std::string TraceCommand::joinArgs(const std::vector<std::string>& a, size_t start)
{
    std::string s;
    for (size_t i = start; i < a.size(); ++i)
    {
        if (i > start)
            s += " ";
        s += a[i];
    }
    return s;
}
