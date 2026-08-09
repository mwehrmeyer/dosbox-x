/*
 *  debug_config.h
 *
 *  Thin interface for loading a JSON file into a data class.
 *
 *  This header is deliberately "light": it pulls in ONLY the standard library
 *  types needed to declare the data class and the loader function. It does NOT
 *  include the JSON parser (nlohmann/json.hpp). That heavy include lives solely
 *  in debug_config.cpp.
 *
 *  Because of this, any translation unit (e.g. debug.cpp) can include this
 *  header to call LoadMyData() without dragging the ~25k-line json.hpp through
 *  its own compilation. Editing debug.cpp therefore never triggers a recompile
 *  of the JSON parser; only editing debug_config.cpp (or json.hpp) does.
 */

#ifndef DOSBOX_DEBUG_CONFIG_H
#define DOSBOX_DEBUG_CONFIG_H

#include <string>
#include <unordered_map>
#include <cstdint>

// The data class we deserialize a JSON file into. Adjust the fields to match
// the shape of your JSON; remember to update the mapping in debug_config.cpp.
struct ComponentData {
	uint32_t entryPoint;
	uint32_t baseAddress;
	uint32_t loadAddress = 0;
	bool isLoadAddressSet = false;
};

struct Kernel {
	uint32_t baseAddress;
	uint32_t hookAddress;
	uint32_t handoffAddress;
	uint32_t hookInstruction;
	uint32_t axValue;
};

struct ComponentListContainer {
	Kernel kernel;
	// Keyed by each component's "name". The JSON is still an array of components;
	// each element is inserted into this map under its "name" during load.
	std::unordered_map<std::string, ComponentData> components;
};

// Load 'path' and deserialize it into 'out'.
// Returns true on success. On failure (file missing or malformed JSON) returns
// false; if 'error' is non-null it receives a human-readable message.
// Implementation (and the only include of json.hpp) lives in debug_config.cpp.
bool LoadComponents(const char* path, ComponentListContainer& out, std::string* error = nullptr);

#endif // DOSBOX_DEBUG_CONFIG_H
