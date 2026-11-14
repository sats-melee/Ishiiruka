#include "Core/Slippi/SatsArenaNetplayRoom.h"
#include <cassert>
#include <iostream>

int main()
{
    SatsArenaNetplay::Room waiting{};
    waiting.players.resize(4);
    assert(SatsArenaNetplay::JoinedPlayers(waiting) == 0);
    waiting.players[0].name = "Host";
    waiting.players[1].name = "Guest";
    assert(SatsArenaNetplay::JoinedPlayers(waiting) == 2);
    waiting.players[1].name.clear();
    assert(SatsArenaNetplay::JoinedPlayers(waiting) == 1);

#ifdef SATS_ARENA
	unsetenv("SATS_ARENA_NETPLAY_FILE");
	unsetenv("SATS_ARENA_NETPLAY_ROOMS");
	setenv("SATS_ARENA_ROOM_SERVER_URL", "http://127.0.0.1:8738", 1);
	assert(!SatsArenaNetplay::Enabled()); // Phone-room configuration alone must not change game mode.
	setenv("SATS_ARENA_NETPLAY_ROOMS", "1", 1);
	assert(SatsArenaNetplay::ServerEnabled() && SatsArenaNetplay::Enabled());
	unsetenv("SATS_ARENA_NETPLAY_ROOMS");
#endif
	std::string normalized;
	for (const auto& input : {"F23E36A4", "f23e36a4", "Ｆ２３Ｅ３６Ａ４", "ｆ２３ｅ３６ａ４"})
	{
		assert(SatsArenaNetplay::NormalizeRoomCode(input, normalized));
		assert(normalized == "F23E36A4");
	}
	for (const auto& input : {"", "F23E36A", "F23E36A44", "F23#36A4", "Ｆ２３Ｇ３６Ａ４"})
		assert(!SatsArenaNetplay::NormalizeRoomCode(input, normalized));
	const auto valid = nlohmann::json::parse(R"({"slot":0,"room":{"build":"test-1","rules":"vanilla-1v1","match_id":"match-one","players":[{"slot":0,"name":"Alice","endpoint":"127.0.0.1:43113","ready":true},{"slot":1,"name":"Bob","endpoint":"127.0.0.1:43114","ready":true}]}})");
	SatsArenaNetplay::Room room;
	std::string error;
	assert(SatsArenaNetplay::Parse(valid, room, error));
	assert(room.slot == 0 && room.match_id == "match-one");
	assert(room.players[1].port == 43114 && room.players[0].address == "127.0.0.1");
	assert(SatsArenaNetplay::PeerEndpoint(room, 1) == "127.0.0.1:43114");
	auto publicRoom = valid;
	publicRoom["room"]["players"][0]["endpoint"] = "203.0.113.5:54000";
	publicRoom["room"]["players"][1]["endpoint"] = "203.0.113.5:54001";
	publicRoom["room"]["players"][0]["lan_endpoint"] = "192.168.1.10:43113";
	publicRoom["room"]["players"][1]["lan_endpoint"] = "192.168.1.11:43114";
	assert(SatsArenaNetplay::Parse(publicRoom, room, error));
	assert(SatsArenaNetplay::PeerEndpoint(room, 1) == "192.168.1.11:43114");
	publicRoom["room"]["players"][1]["endpoint"] = "198.51.100.7:55000";
	assert(SatsArenaNetplay::Parse(publicRoom, room, error));
	assert(SatsArenaNetplay::PeerEndpoint(room, 1) == "198.51.100.7:55000");
	publicRoom["room"]["players"][1]["lan_endpoint"] = "0.0.0.0:43114";
	assert(!SatsArenaNetplay::Parse(publicRoom, room, error));
	auto rejects = [](const nlohmann::json& value) {
		SatsArenaNetplay::Room room;
		std::string error;
		assert(!SatsArenaNetplay::Parse(value, room, error));
		assert(!error.empty());
	};
	for (auto endpoint : {"127.0.0.1:0", "127.0.0.1:65536", "0.0.0.0:12", "224.0.0.1:12", "127.0.0.256:12", "127.0.1:12", "localhost:12", "[::1]:12", "127.0.0.1:-1", "127.0.0.1:43114", "127.0.0.01:12"})
	{
		auto invalid = valid;
		invalid["room"]["players"][0]["endpoint"] = endpoint;
		rejects(invalid);
	}
	auto invalid = valid;
	invalid["slot"] = 2; rejects(invalid);
	invalid = valid; invalid["slot"] = 0.0; rejects(invalid);
	rejects(nlohmann::json::parse("{", nullptr, false));
	invalid = valid; invalid["room"]["players"][1]["slot"] = 0; rejects(invalid);
	invalid = valid; invalid["room"]["players"][1]["ready"] = false; rejects(invalid);
	invalid = valid; invalid["room"]["match_id"] = nullptr; rejects(invalid);
	invalid = valid; invalid["room"]["match_id"] = std::string(51, 'a');
	assert(SatsArenaNetplay::Parse(invalid, room, error));
	invalid["room"]["match_id"] = std::string(52, 'a'); rejects(invalid);
	invalid["room"]["match_id"] = std::string(64, 'a'); rejects(invalid);
	invalid = valid; invalid["room"]["players"].push_back(valid["room"]["players"][0]); rejects(invalid);
	invalid = valid; invalid["room"]["players"][0]["name"] = ""; rejects(invalid);
	for (unsigned count = 3; count <= 4; ++count)
	{
		auto multiple = valid;
		SatsArenaNetplay::Config config;
		config.player_count = count;
		multiple["room"]["config"] = SatsArenaNetplay::ConfigJson(config);
		for (unsigned i = 2; i < count; ++i)
			multiple["room"]["players"].push_back({{"slot", i}, {"name", "Friend"},
			    {"endpoint", "127.0.0.1:" + std::to_string(43113 + i)}, {"ready", true}});
		multiple["slot"] = count - 1;
		assert(SatsArenaNetplay::Parse(multiple, room, error));
		assert(room.players.size() == count && room.slot == static_cast<int>(count - 1));
		config.teams = true;
		for (unsigned i = 0; i < count; ++i) config.team_ids[i] = i % 2;
		multiple["room"]["config"] = SatsArenaNetplay::ConfigJson(config);
		assert(SatsArenaNetplay::Parse(multiple, room, error));
		assert(room.config.teams && room.config.team_ids[2] == 0);
		auto bad = multiple;
		bad["room"]["players"][count - 1]["endpoint"] = "127.0.0.1:43113"; rejects(bad);
		bad = multiple; bad["room"]["players"].erase(count - 1); rejects(bad);
		bad = multiple; bad["room"]["config"]["team_ids"] = {0, 0, 0, 0}; rejects(bad);
		bad = multiple; bad["room"]["config"]["player_count"] = 5; rejects(bad);
		bad = multiple; bad["room"]["config"]["team_ids"][0] = 3; rejects(bad);
	}
	std::cout << "LAN room descriptor validation passed\n";
}
