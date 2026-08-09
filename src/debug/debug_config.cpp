/*
 *  debug_config.cpp
 *
 *  This is the ONLY translation unit that includes nlohmann/json.hpp. The heavy
 *  parser is compiled into debug_config.o exactly once and is not recompiled
 *  when other debug sources (e.g. debug.cpp) change.
 *
 *  To change the data class, edit MyData in debug_config.h and update the
 *  NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE field list below to match.
 */

#include "debug_config.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <cctype>

// ComponentData uses a hand-written from_json so address fields can be given
// either as a JSON number (decimal) or as a quoted string. A string is parsed
// with base 0, so "0x401000" (hex), "0o..."/"0..." (octal) and plain decimal
// all work. Note: a *bare* 0x401000 is still rejected - it is not legal JSON,
// so hex must be quoted.
static uint32_t parse_u32(const nlohmann::json& v)
{
	if (v.is_number_unsigned())
		return v.get<uint32_t>();
	if (v.is_string()) {
		const std::string s = v.get<std::string>();
		size_t consumed = 0;
		unsigned long parsed = std::stoul(s, &consumed, 0); // base 0 => auto-detect 0x..
		if (consumed != s.size())
			throw nlohmann::json::type_error::create(302, "invalid numeric string: " + s, &v);
		return static_cast<uint32_t>(parsed);
	}
	throw nlohmann::json::type_error::create(302, "value must be a number or numeric string", &v);
}

static void from_json(const nlohmann::json& j, ComponentData& c)
{
	c.entryPoint  = parse_u32(j.at("entryPoint"));
	c.baseAddress = parse_u32(j.at("baseAddress"));
}

static void from_json(const nlohmann::json& j, Kernel& k)
{
	k.baseAddress  = parse_u32(j.at("baseAddress"));
	k.hookAddress = parse_u32(j.at("hookAddress"));
	k.handoffAddress = parse_u32(j.at("handoffAddress"));
	k.hookInstruction = parse_u32(j.at("hookInstruction"));
	k.axValue = parse_u32(j.at("axValue"));
}

// "components" is a JSON object mapping name -> {entryPoint, baseAddress}. We
// iterate key/value pairs: the key is the component name (upper-cased here), the
// value deserializes into ComponentData via its from_json (defined above).
static void from_json(const nlohmann::json& j, ComponentListContainer& c)
{
	j.at("kernel").get_to(c.kernel);

	c.components.clear();
	for (auto it = j.at("components").begin(); it != j.at("components").end(); ++it) {
		std::string name = it.key();

        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) {
	                       return std::toupper(c);
                       });
		c.components[name] = it.value().get<ComponentData>();
	}
}

bool LoadComponents(const char* path, ComponentListContainer& out, std::string* error) {
	std::ifstream f(path);
	if (!f) {
		if (error) *error = std::string("cannot open file: ") + path;
		return false;
	}

	try {
		nlohmann::json j;
		f >> j;                 // parse the file
		out = j.get<ComponentListContainer>();  // deserialize into the data class
		return true;
	}
	catch (const nlohmann::json::exception& e) {
		if (error) *error = e.what();
		return false;
	}
}
