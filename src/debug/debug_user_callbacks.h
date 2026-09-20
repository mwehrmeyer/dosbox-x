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
#include <iomanip>
#include <fstream>
#include <list>

void DEBUG_ShowMsg(char const* format,...);

template<typename T>
struct Result {
    T value;
    bool success;
};

template <>
struct Result<void> {
    bool success;
};

template <
    typename T,
    typename = typename std::enable_if<std::is_scalar<T>::value>::type
>
struct DataRecord {
    T reg;
    std::string state{};

    static constexpr std::size_t fieldLength = sizeof(T);
};

// Registers every compiled-in callback with the RUNC registry. Called once, from
// DEBUG_SetupConsole(). Implemented in debug_user_callbacks.cpp.
void DEBUG_RegisterUserCallbacks(void);

template <typename T>
Result<void> flush(std::list<DataRecord<T>> &dataRecord, std::string filename) {
    std::ofstream file;

    file.open(filename.c_str());

    if (!file.is_open()) {
        DEBUG_ShowMsg("DEBUG: cannot open flushFile for writing: %s", "flushFile");
        return Result<void> {false};
    }

    for (auto &it: dataRecord) {
        printHex(file, it);
    }

    file.close();

    return Result<void> {true};
}

template <
    typename T,
    typename = typename std::enable_if<std::is_scalar<T>::value>::type
>
void printHex(std::ofstream &file, const DataRecord<T> &it) {
    file
        << std::uppercase
        << std::hex
        << std::setw(sizeof(T) * 2)
        << std::setfill('0')
        << static_cast<int>(it.reg)
        << std::dec
        << " "
        << it.state
        << std::endl;
}

#endif // DOSBOX_DEBUG_USER_CALLBACKS_H
