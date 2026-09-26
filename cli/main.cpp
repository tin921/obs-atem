/*
 * atem-cli — diagnostics for the ATEM SDK layer without OBS or Qt.
 *
 *   atem-cli [--ip ADDRESS] [info | pip | run N | stop]
 *
 *   info   (default) model, macros, inputs and PiP state
 *   pip    inputs and PiP state only
 *   run N  run macro N (1-based, as numbered in the panel)
 *   stop   stop the running macro
 *
 * Connects over USB unless --ip is given. Output is mirrored to atem-cli.log.
 */

#include "../atem-controller.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <string>

static std::ofstream g_log;

static std::string timestamp()
{
    auto now  = std::chrono::system_clock::now();
    auto ms   = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    auto t    = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    std::ostringstream ss;
    ss << buf << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

static void writeLog(const char* level, const std::string& msg)
{
    std::ostringstream ss;
    ss << '[' << timestamp() << "] [" << std::left << std::setw(5) << level << "] " << msg;
    std::string line = ss.str();

    std::cout << line << '\n';
    if (g_log.is_open()) { g_log << line << '\n'; g_log.flush(); }
}

#define LOG(msg)       writeLog("INFO",  (msg))
#define LOG_ERR(msg)   writeLog("ERROR", (msg))

static void usage()
{
    std::cout << "usage: atem-cli [--ip ADDRESS] [info | pip | run N | stop]\n";
}

static void printMacros(AtemController& atem)
{
    auto macros = atem.getMacros();
    LOG("Macros: " + std::to_string(macros.size()));
    for (const auto& m : macros) {
        std::string line = "  #" + std::to_string(m.index + 1) + " \"" + m.name + '"';
        if (!m.description.empty()) line += "  (" + m.description + ")";
        if (m.hasUnsupportedOps) line += "  [unsupported ops]";
        LOG(line);
    }
}

static std::string inputName(const std::vector<AtemInputInfo>& inputs, BMDSwitcherInputId id)
{
    for (const auto& in : inputs)
        if (in.id == id) return in.longName + " (" + std::to_string(in.id) + ")";
    return "id " + std::to_string(id);
}

static void printPip(AtemController& atem)
{
    AtemPip& pip = atem.pip();
    auto inputs = pip.inputs();
    LOG("Inputs: " + std::to_string(inputs.size()));
    for (const auto& in : inputs) {
        LOG("  " + std::to_string(in.id) + "  " + in.shortName + " / " + in.longName +
            (in.canBeProgram ? "  [main]" : "") + (in.canBePip ? "  [pip]" : ""));
    }

    AtemPipState s = pip.state();
    if (!s.available) {
        LOG_ERR("PiP not available (no mix effect block or upstream key)");
        return;
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(3);
    ss << "PiP state:\n"
       << "  main input   " << inputName(inputs, s.programInput) << '\n'
       << "  pip input    " << inputName(inputs, s.pipInput) << '\n'
       << "  key type DVE " << (s.isDVE ? "yes" : "no") << " (can be DVE: " << (s.canBeDVE ? "yes" : "no") << ")\n"
       << "  on air       " << (s.onAir ? "yes" : "no") << '\n'
       << "  position     x=" << s.positionX << " y=" << s.positionY << '\n'
       << "  size         x=" << s.sizeX << " y=" << s.sizeY << " (can scale up: " << (s.canScaleUp ? "yes" : "no") << ")\n"
       << "  crop         " << (s.cropEnabled ? "on" : "off") << " top=" << s.cropTop << " bottom=" << s.cropBottom
       << " left=" << s.cropLeft << " right=" << s.cropRight << '\n'
       << "  border       " << (s.borderEnabled ? "on" : "off");
    LOG(ss.str());
}

int main(int argc, char** argv)
{
    std::string ip;
    std::string command = "info";
    int macroNumber = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--ip" && i + 1 < argc) {
            ip = argv[++i];
        } else if (arg == "info" || arg == "pip" || arg == "stop") {
            command = arg;
        } else if (arg == "run" && i + 1 < argc) {
            command = arg;
            macroNumber = std::atoi(argv[++i]);
        } else {
            usage();
            return 2;
        }
    }
    if (command == "run" && macroNumber < 1) {
        usage();
        return 2;
    }

    // COM for the BMD SDK. Deliberately never uninitialised: CoUninitialize
    // while the SDK's network threads wind down crashes the process; exiting
    // cleans up instead.
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    g_log.open("atem-cli.log", std::ios::trunc);
    LOG("=== atem-cli " + command + " ===");

    AtemController atem;
    atem.setTraceCallback([](const std::string& msg) { writeLog("TRACE", msg); });

    bool ok = ip.empty() ? atem.connectUSB() : atem.connectIP(ip);
    if (!ok) {
        LOG_ERR("Connection failed: " + atem.lastError());
        return 1;
    }
    LOG("Connected — model: " + atem.modelName() + ", address: " + atem.connectedAddress());

    if (command == "info") {
        printMacros(atem);
        printPip(atem);
    } else if (command == "pip") {
        printPip(atem);
    } else if (command == "run") {
        if (!atem.runMacro(static_cast<uint32_t>(macroNumber - 1))) {
            LOG_ERR("Run failed for macro #" + std::to_string(macroNumber));
            return 1;
        }
        LOG("Started macro #" + std::to_string(macroNumber));
    } else if (command == "stop") {
        if (!atem.stopMacro()) {
            LOG_ERR("Stop failed");
            return 1;
        }
        LOG("Stopped");
    }

    atem.disconnect();
    return 0;
}
