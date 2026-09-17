/*
 *  debug_user_callbacks.h
 *
 *  Public interface of the user-callback module. The callbacks themselves live in
 *  debug_user_callbacks.cpp - that is the file you edit; this header only exposes
 *  the registration entry point so debug.cpp can call it from DEBUG_SetupConsole().
 *
 *  See debug_user_callbacks.cpp for how to write and register a callback.
 */

#ifndef DOSBOX_DEBUG_USER_CALLBACKS_H
#define DOSBOX_DEBUG_USER_CALLBACKS_H

#include <string>
#include <cstdint>

struct DataRecord {
    uint8_t al{};
    std::string state{};
};

// Registers every compiled-in callback with the RUNC registry. Called once, from
// DEBUG_SetupConsole(). Implemented in debug_user_callbacks.cpp.
void DEBUG_RegisterUserCallbacks(void);

#endif // DOSBOX_DEBUG_USER_CALLBACKS_H
