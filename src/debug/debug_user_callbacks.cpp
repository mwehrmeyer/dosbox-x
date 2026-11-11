/*
 *  debug_user_callbacks.cpp
 *
 *  This is the place to add your own compiled-in breakpoint callbacks.
 *
 *  A callback is a plain C++ function that the debugger calls every time the CPU reaches
 *  an instruction you bound it to with the "RUNC" command, e.g.:
 *
 *      RUNC C13216 45AAB8 log_regs
 *
 *  Anything after the callback name is passed to the callback as a list of strings on
 *  every hit, e.g. "RUNC C13216 45AAB8 hello FOO 12" calls hello with args {"FOO","12"}.
 *  Arguments keep the case they were typed in.
 *
 *  A callback can stop in the debugger, like a breakpoint, by calling DEBUG_RequestBreak().
 *  See cb_break_str for a conditional breakpoint built that way.
 *
 *  After the callback returns, emulation continues (it is a logpoint/tracepoint, not a
 *  stop). Because the callback is compiled into DOSBox-X, it has full, direct access to
 *  the emulator: CPU registers (reg_eax, reg_ax, SegValue(cs), ...), guest memory
 *  (mem_readb_checked / mem_writeb / GetAddress(seg,off)), logging (DEBUG_ShowMsg /
 *  LOG_MSG), the loaded component map (componentContainer) and even the debugger command
 *  interpreter (ParseCommand).
 *
 *  To add a callback:
 *    1. Write a function with the signature
 *           void my_cb(uint16_t seg, uint32_t off, const vector<string>& args)
 *    2. Register it inside DEBUG_RegisterUserCallbacks() below.
 *    3. Rebuild DOSBox-X. The name you register is what you pass to RUNC
 *       (matched case-insensitively).
 *
 *  This is a normal translation unit, not a header pasted into debug.cpp, so it only sees
 *  what it includes. Anything it needs from debug.cpp (GetAddress, ParseCommand,
 *  componentContainer, ...) is declared in debug_inc.h; add a declaration there if you
 *  reach for something else.
 */

#include <cstdint>

#include "debug_inc.h"
#include "dosbox.h"
#include "../../include/logging.h"
#include "../../include/paging.h"

#include "debug_user_callbacks.h"

#if C_HEAVY_DEBUG

#include <stdio.h>
#include <cstdlib>
#include <iostream>
#include <list>
#include <string>
#include <vector>

#include "logging.h"
#include "regs.h"
#include "mem.h"
#include "paging.h"
#include "debug.h"
#include "debug_inc.h"
#include "debug_user_callbacks.h"

using namespace std;

std::list<DataRecord<uint8_t>> dataRecordList8;
std::list<DataRecord<uint16_t>> dataRecordList16;
std::list<DataRecord<uint32_t>> dataRecordList32;

static const uint32_t stateStringAddress = 0x45AAB8;

static Result<uint32_t> find32BitRegister(string& registerName) {
    string registerNameLowerCase = registerName;

    std::transform(registerNameLowerCase.begin(), registerNameLowerCase.end(), registerNameLowerCase.begin(), ::tolower);

    uint32_t regValue;

    if (registerNameLowerCase == "esi") {
        regValue = reg_esi;
    } else if (registerNameLowerCase == "eax") {
        regValue = reg_eax;
    } else {
        DEBUG_ShowMsg("Unknown register name %s\n", registerName.c_str());
        return Result<uint32_t> {false, 0};
    }

    return Result<uint32_t> {true, regValue};
}

static bool unescapeString(std::string& dest, std::string& src) {
    size_t sourceIndex = 0;
    while (sourceIndex < src.size()) {
        unsigned char newChar;
        char myChar = src[sourceIndex];
        if (myChar == '\\') {
            if (sourceIndex + 1 < src.size()) {
                char nextChar = src[sourceIndex + 1];
                switch (nextChar) {
                    case 'f':
                        newChar = '\f';
                        sourceIndex += 2;
                        break;
                    case 'n':
                        newChar = '\n';
                        sourceIndex += 2;
                        break;
                    case 'r':
                        newChar = '\r';
                        sourceIndex += 2;
                        break;
                    case 't':
                        newChar = '\t';
                        sourceIndex += 2;
                        break;
                    case 'v':
                        newChar = '\v';
                        sourceIndex += 2;
                        break;
                    case '\\':
                        newChar = '\\';
                        sourceIndex += 2;
                        break;
                    case 'x':
                        if (sourceIndex + 3 >= src.size()) {
                            return false;
                        }

                        std::sscanf(src.c_str() + sourceIndex + 2, "%2hhx", &newChar);
                        sourceIndex += 4;
                        break;
                    default:
                        return false;
                }
            } else {
                return false;
            }
        } else {
            newChar = myChar;
            sourceIndex++;
        }

        dest.push_back(newChar);
    }

    return true;
}

// --- Example callbacks ------------------------------------------------------------------

// A minimal callback: count how often the instruction was reached and print AX each time,
// along with any extra arguments given on the RUNC command line.
static void cb_hello(uint16_t seg, uint32_t off, const vector<string>& args) {
    static unsigned long hits = 0;
    hits++;
    DEBUG_ShowMsg("RUNC hello: hit #%lu at %04X:%04X, AX=%04X\n", hits, seg, off, reg_ax);
    for (size_t i = 0; i < args.size(); i++) {
        DEBUG_ShowMsg("RUNC hello:   arg[%zu] = %s\n", i, args[i].c_str());
    }
    printf("hello you freak bitches\n");
}

// Log the main general-purpose registers each time the instruction is executed.
static void cb_log_regs(uint16_t seg, uint32_t off, const vector<string>& /*args*/) {
    DEBUG_ShowMsg("RUNC %04X:%04X  AX=%04X BX=%04X CX=%04X DX=%04X SI=%04X DI=%04X\n",
                  seg, off, reg_ax, reg_bx, reg_cx, reg_dx, reg_si, reg_di);
}

static void writeEscapedString(string& dest, uint32_t stringAddress) {
    bool lcLastCharWasEscaped = false;
    char lcBuf[20];

    for (size_t i = 0; i < 32; i++) {
        char lcChar;
        uint8_t lcU8;

        if (mem_readb_checked((PhysPt) GetAddress(SegValue(ds), stringAddress + i), &lcU8)) {
            DEBUG_ShowMsg("Cannot read from %X + %lX", stringAddress, i);
            return;
        }

        lcChar = static_cast<char>(lcU8);
        if (lcChar == '\0') {
            break;
        }

        if (isspace(lcChar)) {
            if (lcChar == ' ') {
                dest.append("\\x20");
                lcLastCharWasEscaped = true;
            } else {
                switch (lcChar) {
                    case '\f':
                        dest.append("\\f");
                        break;
                    case '\n':
                        dest.append("\\n");
                        break;
                    case '\r':
                        dest.append("\\r");
                        break;
                    case '\t':
                        dest.append("\\t");
                        break;
                    case '\v':
                        dest.append("\\v");
                        break;
                }
            }
        } else if (lcLastCharWasEscaped && isxdigit(lcChar)) {
            std::snprintf(lcBuf, sizeof(lcBuf), "\\x%02X", lcChar);
            dest += lcBuf;
            lcLastCharWasEscaped = false;
        } else {
            dest += lcChar;
            lcLastCharWasEscaped = false;
        }
    }
}

static void cb_log_al_and_state(uint16_t seg, uint32_t off, const vector<string>& /*args*/) {
    (void) seg;
    (void) off;

    auto it = componentContainer.components.find("C13216");
    if (it == componentContainer.components.end()) {
        return;
    }

    auto componentData = &it->second;
    if (!componentData->isLoadAddressSet) {
        DEBUG_ShowMsg("RUNC: C13216 is not initialized");
        return;
    }

    uint32_t translatedAddress = componentData->loadAddress + (stateStringAddress - componentData->baseAddress);
    uint32_t stringPointer;

    if (mem_readd_checked((PhysPt) GetAddress(SegValue(ds), translatedAddress), &stringPointer)) {
        DEBUG_ShowMsg("RUNC: Translated %x to %x, but memory is not available", stateStringAddress, translatedAddress);
        return;
    }

    string escapedString;
    writeEscapedString(escapedString, stringPointer);

    dataRecordList8.push_back(DataRecord<uint8_t>{reg_al, escapedString});
}

static void cb_log_ax_and_state(uint16_t seg, uint32_t off, const vector<string>& /*args*/) {
    (void) seg;
    (void) off;

    auto it = componentContainer.components.find("C13216");
    if (it == componentContainer.components.end()) {
        return;
    }

    auto componentData = &it->second;
    if (!componentData->isLoadAddressSet) {
        DEBUG_ShowMsg("RUNC: C13216 is not initialized");
    }

    uint32_t translatedAddress = componentData->loadAddress + (stateStringAddress - componentData->baseAddress);
    uint32_t stringPointer;

    if (mem_readd_checked((PhysPt) GetAddress(SegValue(ds), translatedAddress), &stringPointer)) {
        DEBUG_ShowMsg("RUNC: Translated %x to %x, but memory is not available", stateStringAddress, translatedAddress);
        return;
    }

    string escapedString;
    writeEscapedString(escapedString, stringPointer);

    dataRecordList16.push_back(DataRecord<uint16_t>{reg_ax, escapedString});
}

static void cb_log_eax_and_state(uint16_t seg, uint32_t off, const vector<string>& /*args*/) {
    (void) seg;
    (void) off;

    auto it = componentContainer.components.find("C13216");
    if (it == componentContainer.components.end()) {
        return;
    }

    auto componentData = &it->second;
    if (!componentData->isLoadAddressSet) {
        DEBUG_ShowMsg("RUNC: C13216 is not initialized");
        return;
    }

    uint32_t translatedAddress = componentData->loadAddress + (stateStringAddress - componentData->baseAddress);
    uint32_t stringPointer;

    if (mem_readd_checked((PhysPt) GetAddress(SegValue(ds), translatedAddress), &stringPointer)) {
        DEBUG_ShowMsg("RUNC: Translated %x to %x, but memory is not available", stateStringAddress, translatedAddress);
        return;
    }

    string escapedString;
    writeEscapedString(escapedString, stringPointer);

    dataRecordList32.push_back(DataRecord<uint32_t>{reg_eax, escapedString});
}

static void cb_record_dblptr_string_by_reg(uint16_t seg, uint32_t off, const vector<string>& args) {
    (void) seg;
    (void) off;

    if (args.size() < 2) {
        DEBUG_ShowMsg("RUNC break_str: usage: break_str <register> <string>\n");
        return;
    }

    string registerName = args[0];

    Result<uint32_t> findRegisterResult = find32BitRegister(registerName);
    if (!findRegisterResult.success) {
        return;
    }

    uint32_t registerValue = findRegisterResult.value;

    string expected = args[1];
    for (size_t i = 2; i < args.size(); i++) expected += " " + args[i];

    uint32_t stringPointer;
    if (mem_readd_checked((PhysPt) GetAddress(SegValue(ds), registerValue), &stringPointer)) {
        DEBUG_ShowMsg("RUNC: Translated %x to %x, but memory is not available", stateStringAddress, reg_esi);
        return;
    }

    string escapedString;
    writeEscapedString(escapedString, stringPointer);

    dataRecordList32.push_back(DataRecord<uint32_t>{0, escapedString});
}

// Log AX plus the first byte that DS:SI points at - shows reading guest memory.
static void cb_log_ax_dssi(uint16_t /*seg*/, uint32_t /*off*/, const vector<string>& /*args*/) {
    uint8_t value = 0;
    mem_readb_checked((PhysPt) GetAddress(SegValue(ds), reg_si), &value);
    DEBUG_ShowMsg("RUNC: AX=%04X  [DS:SI]=%02X\n", reg_ax, value);
}

static Result<std::vector<std::string>> genericArgumentPreparer(std::istringstream& argStream) {
    std::string arg;
    std::vector<std::string> args;

    while (argStream >> arg) {
        args.push_back(arg);
    }

    return Result<std::vector<std::string> >{true, args};
}

static Result<std::vector<std::string>> prepare_args_for_break_dblptr_str_by_component_offset(std::istringstream& argStream) {
    std::string componentName;
    std::string offsetString;
    std::string untreatedString;
    std::vector<std::string> args;

    if (!(argStream >> componentName >> offsetString >> untreatedString)) {
        DEBUG_ShowMsg("break_dblptr_str_by_component_offset: usage: break_str <component> <offset> <string>\n");
        return Result<std::vector<std::string> >{false, {}};
    }

    std::string stringPart;
    while (argStream >> stringPart) {
        untreatedString += " " + stringPart;
    }

    std::string escapedTargetString;
    if (!unescapeString(escapedTargetString, untreatedString)) {
        return Result<std::vector<std::string> >{false, {}};
    }

    auto argList = std::vector<std::string>{componentName, offsetString, escapedTargetString};
    return Result<std::vector<std::string> > {true, argList};
}

// Conditional breakpoint: stop in the debugger if a 4-byte pointer variable (a DS offset,
// stored at <offset> inside <component>) points to a string equal to <string>, e.g.:
//     RUNC C13216 41234C break_str C13216 45AAB8 Main Menu
// The rest of the line is the string, so it may contain single spaces. Matching is exact
// and case-sensitive: the guest string must end (NUL) right after the expected text.
static void cb_break_dblptr_str_by_component_offset(uint16_t /*seg*/, uint32_t /*off*/, const vector<string>& args) {
    std::string componentName;
    std::string offsetString;
    std::string needle;

    componentName = args[0];
    offsetString = args[1];
    needle = args[2];

    char *end;
    uint32_t offset = std::strtoul(offsetString.c_str(), &end, 16);
    if (end == offsetString.c_str()) {
        DEBUG_ShowMsg("RUNC break_str: %s is not a hex number\n", args[1].c_str());
        return;
    }

    Result<uint32_t> variableAddressResult = calculateAddressRelativeToComponent(componentName, offsetString, offset);
    if (!variableAddressResult.success) {
        return;
    }

    uint32_t stringPointer;
    if (mem_readd_checked((PhysPt) GetAddress(SegValue(ds), variableAddressResult.value), &stringPointer)) {
        return;
    }

    // Compare the expected characters plus the terminating NUL.
    for (size_t i = 0; i < needle.size(); i++) {
        uint8_t haystackChar;
        if (mem_readb_checked((PhysPt) GetAddress(SegValue(ds), stringPointer + (uint32_t) i), &haystackChar)) {
            return;
        }

        uint8_t needleChar = i < needle.size() ? (uint8_t) needle[i] : 0;
        if (tolower(needleChar) != tolower(haystackChar)) {
            return;
        }
    }

    DEBUG_RequestBreak();
}

static Result<std::vector<std::string>> prepare_args_for_break_dblptr_str_by_reg(std::istringstream& argStream) {
    string registerName;
    string unescapedTargetString;

    if (!(argStream >> registerName >> unescapedTargetString)) {
        DEBUG_ShowMsg("RUNC break_str: usage: break_dblptr_str_by_reg <register> <string>\n");
        return Result<std::vector<std::string> >{false, {}};
    }

    string arg;
    while (argStream >> arg) {
        unescapedTargetString += " " + arg;
    }

    Result<uint32_t> findRegisterResult = find32BitRegister(registerName);
    if (!findRegisterResult.success) {
        return Result<std::vector<std::string> >{false, {}};
    }

    std::string escapedTargetString;
    if (!unescapeString(escapedTargetString, unescapedTargetString)) {
        return Result<std::vector<std::string> >{false, {}};
    }

    auto argList = std::vector<std::string>{registerName, escapedTargetString};
    return Result<std::vector<std::string> >{true, argList};
}

static void cb_break_dblptr_str_by_reg(uint16_t /*seg*/, uint32_t /*off*/, const vector<string>& args) {
    std::string registerName;
    std::string escapedTargetString;

    registerName = args[0];
    escapedTargetString = args[1];

    Result<uint32_t> findRegisterResult = find32BitRegister(registerName);
    uint32_t registerValue = findRegisterResult.value;

    uint32_t stringPointer;
    if (mem_readd_checked((PhysPt) GetAddress(SegValue(ds), registerValue), &stringPointer)) {
        return;
    }

    // Compare the expected characters plus the terminating NUL.
    for (size_t i = 0; i < escapedTargetString.size(); i++) {
        uint8_t byte;
        if (mem_readb_checked((PhysPt) GetAddress(SegValue(ds), stringPointer + (uint32_t) i), &byte)) {
            return;
        }

        uint8_t wanted = i < escapedTargetString.size() ? (uint8_t) escapedTargetString[i] : 0;
        if (tolower(byte) != tolower(wanted)) {
            return;
        }
    }

    DEBUG_RequestBreak();
}

// Drive existing debugger commands from native code - shows the ParseCommand passthrough.
static void cb_dump_stack(uint16_t /*seg*/, uint32_t /*off*/, const vector<string>& /*args*/) {
    char cmd[] = "EV SP"; // ParseCommand needs a writable buffer; it copies internally
    ParseCommand(cmd);
}

// Drive existing debugger commands from native code - shows the ParseCommand passthrough.
static void cb_al_and_some_var(uint16_t /*seg*/, uint32_t /*off*/, const vector<string>& /*args*/) {
    char cmd[] = "EV SP"; // ParseCommand needs a writable buffer; it copies internally
    ParseCommand(cmd);
}

// --- Registration -----------------------------------------------------------------------
// Add a DEBUG_RegisterCallback line for every callback you want to expose to RUNC.
void DEBUG_RegisterUserCallbacks(void) {
    DEBUG_RegisterCallback("hello", cb_hello, "Add description here", genericArgumentPreparer);
    DEBUG_RegisterCallback("log_regs", cb_log_regs,
        "Log the main general-purpose registers each time the instruction is executed.", genericArgumentPreparer);
    DEBUG_RegisterCallback("al", cb_log_al_and_state,
        "Records the value of al and the contents of the string that g45AAB8 points to.", genericArgumentPreparer);
    DEBUG_RegisterCallback("ax", cb_log_ax_and_state,
        "Records the value of ax and the contents of the string that g45AAB8 points to.", genericArgumentPreparer);
    DEBUG_RegisterCallback("eax", cb_log_eax_and_state,
        "Records the value of eax and the contents of the string that g45AAB8 points to.", genericArgumentPreparer);
    DEBUG_RegisterCallback("record_dblptr_string_by_reg", cb_record_dblptr_string_by_reg,
        "Records the value of <register> and the contents of the string that <register> points to.", genericArgumentPreparer);
    DEBUG_RegisterCallback("log_ax_dssi", cb_log_ax_dssi, "God knows", genericArgumentPreparer);
    DEBUG_RegisterCallback("dump_stack", cb_dump_stack, "God knows", genericArgumentPreparer);
    DEBUG_RegisterCallback("break_dblptr_str_by_component_offset", cb_break_dblptr_str_by_component_offset,
        "Breaks debugger if <component>:<offset> contains a pointer to a string matching <needle>",
        prepare_args_for_break_dblptr_str_by_component_offset);
    DEBUG_RegisterCallback("break_dblptr_str_by_reg", cb_break_dblptr_str_by_reg,
        "Breaks debugger if ds:<register> contains a pointer to a string matching <needle>",
        prepare_args_for_break_dblptr_str_by_reg);

    // >>> Register your own callbacks here <<<
}
#endif
