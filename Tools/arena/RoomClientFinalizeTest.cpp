#include "Core/Slippi/SatsArenaRoomClient.h"
#include "Core/Slippi/SatsArenaTracker.h"

#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

extern const std::string scm_rev_git_str = "room-finalize-self-test";
using Bytes = std::vector<std::uint8_t>;

static void Word(Bytes& data, std::size_t offset, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i) data[offset + i] = static_cast<std::uint8_t>(value >> (24 - i * 8));
}

static void Feed(const Bytes& data)
{
    SatsArena::GetTracker().Process(data.data(), data.size());
}

static void CompleteFourPlayerGame(const std::string& id = std::string(32, 'a'), unsigned gameNumber = 1)
{
    Bytes start(0x2f9);
    start[0] = 0x36;
    start[0x1a4] = 8;
    start[0x14] = 31;
    Word(start, 0x13d, 42);
    Word(start, 0x2f1, gameNumber);
    std::memcpy(start.data() + 0x2be, id.c_str(), id.size() + 1);
    for (unsigned player = 0; player < 4; ++player)
    {
        start[0x66 + player * 0x24] = 0;
        start[0x65 + player * 0x24] = 17;
    }
    Feed(start);
    for (int frame = -123; frame <= 1; ++frame)
    {
        for (unsigned player = 0; player < 4; ++player)
        {
            Bytes post(0x55);
            post[0] = 0x38;
            Word(post, 1, static_cast<std::uint32_t>(frame));
            post[5] = player;
            post[0x21] = 4;
            Feed(post);
        }
        Bytes credit(SatsArena::CREDIT_PACKET_SIZE);
        credit[0] = SatsArena::CREDIT_COMMAND;
        credit[1] = 1;
        Word(credit, 4, static_cast<std::uint32_t>(frame));
        if (frame == 1)
        {
            credit[2] = 1;
            credit[8] = 3;
            credit[10] = 2;
            float damage = 12.5f;
            std::uint32_t bits;
            std::memcpy(&bits, &damage, sizeof(bits));
            Word(credit, 16, bits);
        }
        Feed(credit);
        Bytes book(9);
        book[0] = 0x3c;
        Word(book, 1, static_cast<std::uint32_t>(frame));
        Word(book, 5, static_cast<std::uint32_t>(frame));
        Feed(book);
    }
    Bytes result(SatsArena::RESULT_PACKET_SIZE);
    result[0] = SatsArena::RESULT_COMMAND;
    result[1] = 1;
    result[2] = 2;
    result[4] = 15;
    result[5] = 8;
    result[6] = 255;
    for (unsigned player = 0; player < 4; ++player) result[12 + player] = 255;
    Feed(result);
    Feed({0x39, 2, 255, 1, 2, 3, 0});
    const auto finished = SatsArena::GetTracker().Read();
    assert(finished.online && finished.game_ended && finished.result_complete);
    assert(finished.total_credit_events == 1 && finished.winner_port == 3);
}

int main(int argc, char** argv)
{
    assert(std::getenv("SATS_ARENA_ROOM_SERVER_URL"));
    SatsArenaNetplay::Config config;
    config.player_count = 4;
    assert(SatsArenaRoomClient::SetCreateConfig(config));
    auto client = std::unique_ptr<SatsArenaRoomClient>(new SatsArenaRoomClient(true, "", "127.0.0.1:43413", 53413));
    assert(client->LocalPort() == 53413 && client->GameplayEndpoint() == "127.0.0.1:43413");
    SatsArenaNetplay::Room room{};
    std::string error;
    for (int i = 0; i < 200 && client->Poll(room, error) != SatsArenaRoomClient::Status::Started; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(client->Poll(room, error) == SatsArenaRoomClient::Status::Started);
    assert(client->HasPayment() && room.players.size() == 4);
    const bool rematch = argc == 2 && std::string(argv[1]) == "--rematch";
    const bool remoteClose = argc == 2 && std::string(argv[1]) == "--remote-close";
    if (remoteClose) std::this_thread::sleep_for(std::chrono::milliseconds(350));
    // Status is published immediately before the 250ms polling wait. End and
    // destroy within that wait, before the next ordinary report opportunity.
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CompleteFourPlayerGame();
    if (rematch)
    {
        const auto code = client->GetRoomCode();
        for (int i = 0; i < 500 && !client->CanNextMatch(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        assert(client->CanNextMatch() && client->NextMatch());
        const auto second = std::string(32, 'b');
        for (int i = 0; i < 500; ++i)
        {
            const auto payment = client->PaymentView();
            if (payment["match_id"] == second && payment["phase"] == 1) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const auto fresh = client->PaymentView();
        assert(fresh["match_id"] == second && fresh["phase"] == 1);
        assert(fresh["players"][0]["paid_sats"] == 0 && fresh["players"][0]["invoice"] == "fresh-entry");
        assert(client->GetRoomCode() == code && client->LocalSlot() == 0);
        assert(client->Poll(room, error) == SatsArenaRoomClient::Status::Waiting && !client->CanNextMatch());
        assert(client->SetAddress("player@example.test"));
        for (int i = 0; i < 500 && client->Poll(room, error) != SatsArenaRoomClient::Status::Started; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        assert(client->Poll(room, error) == SatsArenaRoomClient::Status::Started && room.match_id == second);
        CompleteFourPlayerGame(second, 2);
        for (int i = 0; i < 500 && !client->CanNextMatch(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        assert(client->CanNextMatch() && client->CloseRoom());
        for (int i = 0; i < 500; ++i)
        {
            client->Poll(room, error);
            if (error == "Temporary close rejection") break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        assert(error == "Temporary close rejection" && !client->IsClosed());
        assert(client->PeerPings().active);
        assert(client->CloseRoom());
        for (int i = 0; i < 500 && !client->IsClosed(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        assert(client->IsClosed());
        assert(!client->PeerPings().active);
        std::cout << "Native room retained membership across fresh paid epochs and acknowledged Close only after retry\n";
        return 0;
    }
    if (remoteClose)
    {
        for (int i = 0; i < 500 && !client->IsClosed(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        assert(client->IsClosed());
        std::cout << "Native room flushed a complete late report after the room disappeared\n";
        return 0;
    }
    const auto before = std::chrono::steady_clock::now();
    client.reset();
    assert(std::chrono::steady_clock::now() - before < std::chrono::seconds(5));
    std::cout << "Native paid client destroyed immediately after a complete four-player result\n";
}
