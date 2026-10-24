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

#include "debug_user_callbacks.h"

#include <cstdint>

#include "debug_inc.h"
#include "debug_inc.h"
#include "debug_inc.h"
#include "dosbox.h"
#include "../../include/logging.h"
#include "../../include/paging.h"
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
                    case '\r':
                        dest.append("\\r");
                        break;
                    case '\n':
                        dest.append("\\n");
                        break;
                    case '\t':
                        dest.append("\\t");
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

// Log AX plus the first byte that DS:SI points at - shows reading guest memory.
static void cb_log_ax_dssi(uint16_t /*seg*/, uint32_t /*off*/, const vector<string>& /*args*/) {
    uint8_t value = 0;
    mem_readb_checked((PhysPt) GetAddress(SegValue(ds), reg_si), &value);
    DEBUG_ShowMsg("RUNC: AX=%04X  [DS:SI]=%02X\n", reg_ax, value);
}

// Conditional breakpoint: stop in the debugger if a 4-byte pointer variable (a DS offset,
// stored at <offset> inside <component>) points to a string equal to <string>, e.g.:
//     RUNC C13216 41234C break_str C13216 45AAB8 Main Menu
// The rest of the line is the string, so it may contain single spaces. Matching is exact
// and case-sensitive: the guest string must end (NUL) right after the expected text.
static void cb_break_str(uint16_t /*seg*/, uint32_t /*off*/, const vector<string>& args) {
    if (args.size() < 3) {
        DEBUG_ShowMsg("RUNC break_str: usage: break_str <component> <offset> <string>\n");
        return;
    }

    string componentName = args[0];
    char *end;
    uint32_t ofs = std::strtoul(args[1].c_str(), &end, 16);
    if (end == args[1].c_str()) {
        DEBUG_ShowMsg("RUNC break_str: %s is not a hex number\n", args[1].c_str());
        return;
    }

    string expected = args[2];
    for (size_t i = 3; i < args.size(); i++) expected += " " + args[i];

    std::string source;
    size_t i = 0;
    while (i < expected.size()) {
        unsigned char newChar;
        char myChar = expected[i];
        if (myChar == '\\') {
            if (i + 1 < expected.size()) {
                char nextChar = expected[i + 1];
                switch (nextChar) {
                    case 'r':
                        newChar = '\r';
                        i += 2;
                        break;
                    case 'n':
                        newChar = '\n';
                        i += 2;
                        break;
                    case 't':
                        newChar = '\t';
                        i += 2;
                        break;
                    case '\'':
                        newChar = '\\';
                        i += 2;
                        break;
                    case 'x':
                        if (i + 3 < expected.size()) {
                            std::sscanf(expected.c_str() + i + 2, "%2hhx", &newChar);
                            i += 4;
                        }
                }
            } else {
                newChar = myChar;
                i++;
            }
        } else {
            newChar = myChar;
            i++;
        }

        source.push_back(newChar);
    }


    Result<uint32_t> variableAddress = calculateAddressRelativeToComponent(componentName, args[1], ofs);
    if (!variableAddress.success) {
        return;
    }

    uint32_t stringPointer;
    if (mem_readd_checked((PhysPt) GetAddress(SegValue(ds), variableAddress.value), &stringPointer)) {
        return;
    }

    // Compare the expected characters plus the terminating NUL.
    for (size_t i = 0; i < source.size(); i++) {
        uint8_t byte;
        if (mem_readb_checked((PhysPt) GetAddress(SegValue(ds), stringPointer + (uint32_t) i), &byte)) {
            return;
        }

        uint8_t wanted = i < source.size() ? (uint8_t) source[i] : 0;
        if (tolower(byte) != tolower(wanted)) {
            return;
        }
    }

    // DEBUG_ShowMsg("RUNC break_str: [%s:%X] -> %X is \"%s\"\n", componentName.c_str(), ofs, stringPointer, expected.c_str());
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
    DEBUG_RegisterCallback("hello", cb_hello);
    DEBUG_RegisterCallback("log_regs", cb_log_regs);
    DEBUG_RegisterCallback("al", cb_log_al_and_state);
    DEBUG_RegisterCallback("ax", cb_log_ax_and_state);
    DEBUG_RegisterCallback("eax", cb_log_eax_and_state);
    DEBUG_RegisterCallback("log_ax_dssi", cb_log_ax_dssi);
    DEBUG_RegisterCallback("dump_stack", cb_dump_stack);
    DEBUG_RegisterCallback("break_str", cb_break_str);

    // >>> Register your own callbacks here <<<
}
#endif
