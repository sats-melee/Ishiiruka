#pragma once

#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include <json.hpp>

namespace SatsArenaNetplay
{
inline bool ServerEnabled()
{
#ifdef SATS_ARENA
	const char* value = std::getenv("SATS_ARENA_NETPLAY_ROOMS");
	return value && std::string(value) == "1";
#else
	return false;
#endif
}

inline bool Enabled()
{
#ifdef SATS_ARENA
	return ServerEnabled() || std::getenv("SATS_ARENA_NETPLAY_FILE") != nullptr;
#else
	return false;
#endif
}

// Melee's name-entry keyboard sends full-width UTF-8 after Shift-JIS conversion.
inline bool NormalizeRoomCode(const std::string& input, std::string& output)
{
	std::string code;
	for (size_t i = 0; i < input.size(); ++i)
	{
		unsigned char c = input[i];
		if (c == 0xEF && i + 2 < input.size())
		{
			const unsigned char second = input[i + 1], third = input[i + 2];
			if (second == 0xBC && third >= 0x90 && third <= 0x99) c = '0' + third - 0x90;
			else if (second == 0xBC && third >= 0xA1 && third <= 0xA6) c = 'A' + third - 0xA1;
			else if (second == 0xBD && third >= 0x81 && third <= 0x86) c = 'A' + third - 0x81;
			else return false;
			i += 2;
		}
		if (c >= 'a' && c <= 'f') c -= 'a' - 'A';
		if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')) || code.size() >= 8)
			return false;
		code.push_back(c);
	}
	if (code.size() != 8) return false;
	output = code;
	return true;
}

struct Player
{
	std::string name;
	std::string address;
	unsigned short port;
	std::string lan_endpoint;
};
struct Config
{
	unsigned player_count = 2;
	bool teams = false;
	std::array<unsigned, 4> team_ids = {{255, 255, 255, 255}};
};

inline bool ParseConfig(const nlohmann::json& value, Config& result)
{
	if (!value.is_object() || !value.count("player_count") || !value["player_count"].is_number_integer() ||
	    value["player_count"] < 2 || value["player_count"] > 4 ||
	    !value.count("teams") || !value["teams"].is_boolean() ||
	    !value.count("team_ids") || !value["team_ids"].is_array() || value["team_ids"].size() != 4)
		return false;
	Config next;
	next.player_count = value["player_count"].get<unsigned>();
	next.teams = value["teams"].get<bool>();
	unsigned sides = 0;
	for (unsigned i = 0; i < 4; ++i)
	{
		const auto& team = value["team_ids"][i];
		if (!team.is_number_integer() || (next.teams && i < next.player_count ?
		    (team < 0 || team > 2) : team != 255)) return false;
		next.team_ids[i] = team.get<unsigned>();
		if (next.teams && i < next.player_count) sides |= 1u << next.team_ids[i];
	}
	if (next.teams && (sides & (sides - 1)) == 0) return false;
	result = next;
	return true;
}

inline nlohmann::json ConfigJson(const Config& value)
{
	return {{"player_count", value.player_count}, {"teams", value.teams}, {"team_ids", value.team_ids}};
}

struct Room
{
	int slot;
	std::uint64_t roster_revision = 0;
	std::string match_id;
	Config config;
	std::vector<Player> players;
};

inline bool Text(const nlohmann::json& object, const char* key, std::string& out, size_t limit)
{
	if (!object.is_object() || !object.count(key) || !object[key].is_string())
		return false;
	out = object[key].get<std::string>();
	if (out.empty() || out.size() > limit)
		return false;
	for (unsigned char c : out)
		if (c < 32 || c == 127)
			return false;
	return true;
}

inline bool Endpoint(const std::string& endpoint, Player& target)
{
	const auto colon = endpoint.find(':');
	if (colon == std::string::npos || endpoint.size() > 21)
		return false;
	target.address = endpoint.substr(0, colon);
	unsigned octets[4] = {};
	size_t cursor = 0;
	for (int j = 0; j < 4; ++j)
	{
		size_t end = j == 3 ? target.address.size() : target.address.find('.', cursor);
		if (end == std::string::npos || end <= cursor || end - cursor > 3)
			return false;
		for (size_t k = cursor; k < end; ++k)
		{
			const char c = target.address[k];
			if (c < '0' || c > '9')
				return false;
			octets[j] = octets[j] * 10 + c - '0';
		}
		if (octets[j] > 255 || (end - cursor > 1 && target.address[cursor] == '0'))
			return false;
		cursor = end + 1;
	}
	if (octets[0] == 0 || octets[0] >= 224)
		return false;
	unsigned port = 0;
	const std::string portText = endpoint.substr(colon + 1);
	if (portText.empty() || portText.size() > 5)
		return false;
	for (char c : portText)
	{
		if (c < '0' || c > '9')
			return false;
		port = port * 10 + c - '0';
	}
	if (port == 0 || port > 65535)
		return false;
	target.port = static_cast<unsigned short>(port);
	return true;
}

inline bool Parse(const nlohmann::json& root, Room& result, std::string& error)
{
	error = "Invalid LAN room descriptor (requires all configured IPv4 players ready)";
	if (!root.is_object() || !root.count("slot") || !root["slot"].is_number_integer() ||
	    root["slot"] < 0 || root["slot"] > 3 || !root.count("room") || !root["room"].is_object())
		return false;
	result.slot = root["slot"].get<int>();
	const auto& room = root["room"];
	result.config = Config();
	if (room.count("config") && !ParseConfig(room["config"], result.config)) return false;
	if (static_cast<unsigned>(result.slot) >= result.config.player_count) return false;
	std::string ignored;
	// Slippi MSRB_MATCH_ID and replay MatchIdLength are 51 bytes.
	if (!Text(room, "match_id", result.match_id, 51) || !Text(room, "build", ignored, 128) ||
	    !Text(room, "rules", ignored, 128) || !room.count("players"))
		return false;
	const auto& players = room["players"];
	if (!players.is_array() || players.size() != result.config.player_count)
		return false;
	result.players.resize(players.size());
	for (size_t i = 0; i < players.size(); ++i)
	{
		const auto& player = players[i];
		std::string endpoint;
		if (!player.is_object() || !player.count("slot") || !player["slot"].is_number_integer() || player["slot"] != i ||
		    !player.count("ready") || !player["ready"].is_boolean() || !player["ready"].get<bool>() ||
		    !Text(player, "name", result.players[i].name, 64) || !Text(player, "endpoint", endpoint, 21) ||
		    !Endpoint(endpoint, result.players[i]))
			return false;
		result.players[i].lan_endpoint.clear();
		if (player.count("lan_endpoint") && !player["lan_endpoint"].is_null())
		{
			Player candidate;
			if (!Text(player, "lan_endpoint", result.players[i].lan_endpoint, 21) ||
			    !Endpoint(result.players[i].lan_endpoint, candidate)) return false;
		}
		for (size_t previous = 0; previous < i; ++previous)
			if (result.players[previous].address == result.players[i].address &&
			    result.players[previous].port == result.players[i].port) return false;
	}
	error.clear();
	return true;
}

inline unsigned JoinedPlayers(const Room& room)
{
    unsigned joined = 0;
    for (const auto& player : room.players)
        joined += !player.name.empty();
    return joined;
}

inline std::string PeerEndpoint(const Room& room, size_t slot)
{
	const auto& peer = room.players[slot];
	const auto& local = room.players[room.slot];
	if (peer.address == local.address && !local.lan_endpoint.empty() && !peer.lan_endpoint.empty())
		return peer.lan_endpoint;
	return peer.address + ":" + std::to_string(peer.port);
}

inline bool Load(Room& result, std::string& error)
{
	error = "Cannot read LAN room descriptor (maximum 4096 bytes)";
	const char* path = std::getenv("SATS_ARENA_NETPLAY_FILE");
	if (!path || !*path)
		return false;
	std::ifstream input(path, std::ios::binary);
	if (!input)
		return false;
	std::string text;
	char byte;
	while (input.get(byte))
	{
		if (text.size() >= 4096)
			return false;
		text.push_back(byte);
	}
	if (!input.eof())
		return false;
	const auto root = nlohmann::json::parse(text, nullptr, false);
	return Parse(root, result, error);
}
}
