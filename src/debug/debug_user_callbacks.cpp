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
 *  After the callback returns, emulation continues (it is a logpoint/tracepoint, not a
 *  stop). Because the callback is compiled into DOSBox-X, it has full, direct access to
 *  the emulator: CPU registers (reg_eax, reg_ax, SegValue(cs), ...), guest memory
 *  (mem_readb_checked / mem_writeb / GetAddress(seg,off)), logging (DEBUG_ShowMsg /
 *  LOG_MSG), the loaded component map (componentContainer) and even the debugger command
 *  interpreter (ParseCommand).
 *
 *  To add a callback:
 *    1. Write a function with the signature  void my_cb(uint16_t seg, uint32_t off)
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
#include <iostream>
#include <list>
#include <string>

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

// A minimal callback: count how often the instruction was reached and print AX each time.
static void cb_hello(uint16_t seg, uint32_t off) {
    static unsigned long hits = 0;
    hits++;
    DEBUG_ShowMsg("RUNC hello: hit #%lu at %04X:%04X, AX=%04X\n", hits, seg, off, reg_ax);
    printf("hello you freak bitches\n");
}

// Log the main general-purpose registers each time the instruction is executed.
static void cb_log_regs(uint16_t seg, uint32_t off) {
    DEBUG_ShowMsg("RUNC %04X:%04X  AX=%04X BX=%04X CX=%04X DX=%04X SI=%04X DI=%04X\n",
                  seg, off, reg_ax, reg_bx, reg_cx, reg_dx, reg_si, reg_di);
}

static void writeEscapedString(string& dest, uint32_t stringAddress) {
    for (size_t i = 0; i < 20; i++) {
        uint8_t byte;

        if (mem_readb_checked((PhysPt) GetAddress(SegValue(ds), stringAddress + i), &byte)
            || byte == 0
        ) {
            break;
        }

        if (byte < ' ' || byte > '~') {
            switch (byte) {
                case '\0':
                    dest += "\\0";
                    break;
                case '\a':
                    dest += "\\a";
                    break;
                case '\b':
                    dest += "\\b";
                    break;
                case '\t':
                    dest += "\\t";
                    break;
                case '\n':
                    dest += "\\n";
                    break;
                case '\v':
                    dest += "\\v";
                    break;
                case '\f':
                    dest += "\\f";
                    break;
                case '\r':
                    dest += "\\r";
                    break;
                default:
                    char escape[5] = {'\0'};
                    sprintf(escape, "\\x%x", byte);
                    dest += escape;
            }
        } else {
            char character = static_cast<char>(byte);
            dest += character;
        }
    }
}

static void cb_log_al_and_state(uint16_t seg, uint32_t off) {
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

static void cb_log_ax_and_state(uint16_t seg, uint32_t off) {
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

static void cb_log_eax_and_state(uint16_t seg, uint32_t off) {
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
static void cb_log_ax_dssi(uint16_t /*seg*/, uint32_t /*off*/) {
    uint8_t value = 0;
    mem_readb_checked((PhysPt) GetAddress(SegValue(ds), reg_si), &value);
    DEBUG_ShowMsg("RUNC: AX=%04X  [DS:SI]=%02X\n", reg_ax, value);
}

// Drive existing debugger commands from native code - shows the ParseCommand passthrough.
static void cb_dump_stack(uint16_t /*seg*/, uint32_t /*off*/) {
    char cmd[] = "EV SP"; // ParseCommand needs a writable buffer; it copies internally
    ParseCommand(cmd);
}

// Drive existing debugger commands from native code - shows the ParseCommand passthrough.
static void cb_al_and_some_var(uint16_t /*seg*/, uint32_t /*off*/) {
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

    // >>> Register your own callbacks here <<<
}
#endif
