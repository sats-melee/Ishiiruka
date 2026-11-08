#include "Core/Slippi/SatsArenaRoomClient.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <set>
#include <thread>
#include <vector>

extern const std::string scm_rev_git_str = "room-client-self-test";

void FourPlayers(bool teams)
{
    SatsArenaNetplay::Config config;
    config.player_count = 4;
    config.teams = teams;
    config.team_ids = teams ? std::array<unsigned, 4>{{0, 1, 0, 1}} :
                             std::array<unsigned, 4>{{255, 255, 255, 255}};
    assert(SatsArenaRoomClient::SetCreateConfig(config));
    std::vector<std::unique_ptr<SatsArenaRoomClient>> clients;
    const unsigned firstPort = teams ? 43323 : 43313;
    auto endpoint = [firstPort](unsigned slot) { return "127.0.0.1:" + std::to_string(firstPort + slot); };
    setenv("SATS_ARENA_ROOM_NAME", "Player 0", 1);
    clients.emplace_back(new SatsArenaRoomClient(true, "", endpoint(0)));
    for (int i = 0; i < 100 && clients[0]->GetRoomCode().empty(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto code = clients[0]->GetRoomCode();
    assert(code.size() == 8);
    SatsArenaNetplay::Room room{};
    std::string error;
    for (unsigned slot = 1; slot < 4; ++slot)
    {
        setenv("SATS_ARENA_ROOM_NAME", ("Player " + std::to_string(slot)).c_str(), 1);
        clients.emplace_back(new SatsArenaRoomClient(false, code, endpoint(slot)));
        for (int i = 0; i < 100 && clients[slot]->LocalSlot() < 0; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        assert(clients[slot]->LocalSlot() == static_cast<int>(slot));
        if (slot < 3)
        {
            // Allow several readiness polls; two or three ready peers cannot start a four-player room.
            std::this_thread::sleep_for(std::chrono::milliseconds(1200));
            for (const auto& client : clients)
            {
                assert(client->Poll(room, error) == SatsArenaRoomClient::Status::Waiting);
                assert(room.players.size() == 4 && room.match_id.empty());
                for (unsigned joined = 0; joined <= slot; ++joined)
                    assert(room.players[joined].name == "Player " + std::to_string(joined));
                for (unsigned empty = slot + 1; empty < 4; ++empty)
                    assert(room.players[empty].name.empty());
            }
        }
        if (slot == 2)
        {
            SatsArenaRoomClient duplicate(false, code, endpoint(slot));
            for (int i = 0; i < 50 && duplicate.Poll(room, error) != SatsArenaRoomClient::Status::Failed; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            assert(duplicate.Poll(room, error) == SatsArenaRoomClient::Status::Failed);
            assert(error == "Gameplay endpoint already claimed");
        }
    }
    for (int i = 0; i < 100; ++i)
    {
        bool started = true;
        for (const auto& client : clients)
            started = started && client->Poll(room, error) == SatsArenaRoomClient::Status::Started;
        if (started) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::string matchId;
    for (unsigned slot = 0; slot < 4; ++slot)
    {
        assert(clients[slot]->Poll(room, error) == SatsArenaRoomClient::Status::Started);
        assert(room.slot == static_cast<int>(slot) && room.players.size() == 4);
        assert(room.match_id.size() == 32);
        if (slot == 0) matchId = room.match_id;
        assert(room.match_id == matchId);
        assert(SatsArenaNetplay::ConfigJson(room.config) == SatsArenaNetplay::ConfigJson(config));
        assert(SatsArenaNetplay::ConfigJson(clients[slot]->GetConfig()) == SatsArenaNetplay::ConfigJson(config));
        std::set<unsigned> ports;
        for (unsigned player = 0; player < 4; ++player)
        {
            assert(room.players[player].address == "127.0.0.1");
            assert(room.players[player].port == firstPort + player);
            ports.insert(room.players[player].port);
        }
        assert(ports.size() == 4);
    }
    clients[0].reset();
    for (int i = 0; i < 100; ++i)
    {
        bool failed = true;
        for (unsigned slot = 1; slot < 4; ++slot)
            failed = failed && clients[slot]->Poll(room, error) == SatsArenaRoomClient::Status::Closed;
        if (failed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    for (unsigned slot = 1; slot < 4; ++slot)
    {
        assert(clients[slot]->Poll(room, error) == SatsArenaRoomClient::Status::Closed);
        assert(!error.empty());
    }
    std::cout << "PASS: four-player " << (teams ? "teams" : "FFA")
              << " waits for every member, preserves slots/endpoints/rules, rejects duplicates, closes all guests\n";
}

int main(int argc, char** argv)
{
    assert(std::getenv("SATS_ARENA_ROOM_SERVER_URL"));
    if (argc == 2 && std::string(argv[1]) == "--four-player")
    {
        FourPlayers(false);
        FourPlayers(true);
        return 0;
    }
    assert(argc == 1);
    assert(SatsArenaRoomClient::SetCreateConfig(SatsArenaNetplay::Config()));
    SatsArenaNetplay::Config config;
    config.player_count = 2;
    assert(SatsArenaRoomClient::SetCreateConfig(config));
    auto host = std::make_unique<SatsArenaRoomClient>(true, "", "127.0.0.1:43213");
    for (int i = 0; i < 100 && host->GetRoomCode().empty(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto code = host->GetRoomCode();
    assert(code.size() == 8);
    // Let the host poll an already-ready room before another player joins.
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    SatsArenaNetplay::Room rejected{};
    std::string rejectedError;
    {
        SatsArenaRoomClient duplicate(false, code, "127.0.0.1:43213");
        for (int i = 0; i < 50 && duplicate.Poll(rejected, rejectedError) != SatsArenaRoomClient::Status::Failed; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        assert(duplicate.Poll(rejected, rejectedError) == SatsArenaRoomClient::Status::Failed);
        assert(rejectedError == "Gameplay endpoint already claimed");
    }
    std::string fullwidthCode;
    for (unsigned char c : code)
    {
        const unsigned point = c + 0xFEE0;
        fullwidthCode += static_cast<char>(0xE0 | (point >> 12));
        fullwidthCode += static_cast<char>(0x80 | ((point >> 6) & 0x3F));
        fullwidthCode += static_cast<char>(0x80 | (point & 0x3F));
    }
    SatsArenaRoomClient guest(false, fullwidthCode, "127.0.0.1:43214");
    SatsArenaNetplay::Room a{}, b{};
    std::string error;
    for (int i = 0; i < 100; ++i)
    {
        if (host->Poll(a, error) == SatsArenaRoomClient::Status::Started &&
            guest.Poll(b, error) == SatsArenaRoomClient::Status::Started) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    assert(host->Poll(a, error) == SatsArenaRoomClient::Status::Started);
    assert(guest.Poll(b, error) == SatsArenaRoomClient::Status::Started);
    assert(a.slot == 0 && b.slot == 1 && a.match_id == b.match_id && a.match_id.size() == 32);
    assert(a.players[1].port == b.players[1].port && a.players[1].port == 43214);
    host.reset();
    for (int i = 0; i < 80 && guest.Poll(b, error) != SatsArenaRoomClient::Status::Closed; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(guest.Poll(b, error) == SatsArenaRoomClient::Status::Closed);
    assert(!error.empty());
    for (const auto& endpoint : {"0.0.0.0:12", "127.0.0.1:0"})
    {
        SatsArenaRoomClient invalid(true, "", endpoint);
        for (int i = 0; i < 20 && invalid.Poll(b, error) != SatsArenaRoomClient::Status::Failed; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        assert(invalid.Poll(b, error) == SatsArenaRoomClient::Status::Failed);
    }
    SatsArenaRoomClient invalid(false, "NOTACODE", "127.0.0.1:43215");
    for (int i = 0; i < 20 && invalid.Poll(b, error) != SatsArenaRoomClient::Status::Failed; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(invalid.Poll(b, error) == SatsArenaRoomClient::Status::Failed);
    std::cout << "PASS: native room create/fullwidth-code join/start, shared match ID, heartbeat/disconnect, invalid address/code\n";
}
