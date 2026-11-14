#include "Core/Slippi/SatsArenaRoomClient.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

extern const std::string scm_rev_git_str = "room-recovery-self-test";

int main(int argc, char** argv)
{
    // Cancelled searches must not clear a newer session's panel.
    auto oldRoom = std::make_shared<SatsArenaRoomClient>(false, "bad", "127.0.0.1:43511");
    auto newRoom = std::make_shared<SatsArenaRoomClient>(false, "bad", "127.0.0.1:43512");
    SatsArenaRoomClient::SetActive(oldRoom);
    SatsArenaRoomClient::ClearActive(oldRoom);
    assert(!SatsArenaRoomClient::Active());
    SatsArenaRoomClient::SetActive(newRoom);
    SatsArenaRoomClient::ClearActive(oldRoom);
    assert(SatsArenaRoomClient::Active() == newRoom);
    SatsArenaRoomClient::ClearActive(newRoom);
    assert(!SatsArenaRoomClient::Active());

    const auto now = std::chrono::steady_clock::now();
    SatsArenaRoomClient::PingSnapshot samples{true, {}};
    assert(samples.WorstMilliseconds(0, 4, now) == 0);
    samples.players[0] = {999000, now};
    samples.players[1] = {194001, now};
    samples.players[2] = {90000, now};
    samples.players[3] = {300000, now};
    assert(samples.WorstMilliseconds(0, 2, now) == 195);
    assert(samples.WorstMilliseconds(1, 3, now) == 999);
    assert(samples.WorstMilliseconds(0, 4, now) == 300);
    assert(samples.WorstMilliseconds(0, 4, now + std::chrono::seconds(3)) == 0);
    assert(samples.WorstMilliseconds(0, 4, now - std::chrono::seconds(1)) == 0);
    samples.players[1] = {1, now};
    assert(samples.players[1].Milliseconds(now) == 1);
    const std::string mode = argc == 2 ? argv[1] : "";
    const bool paid = mode == "paid";
    assert(SatsArenaRoomClient::SetCreateConfig(SatsArenaNetplay::Config()));
    auto client = std::make_unique<SatsArenaRoomClient>(true, "", "127.0.0.1:43513", 53513,
                                                      "192.168.1.10:53513");
    SatsArenaNetplay::Room room{};
    std::string error;
    bool reconnecting = false, recovered = false;
    for (int i = 0; i < 150; ++i)
    {
        const auto status = client->Poll(room, error);
        assert(status != SatsArenaRoomClient::Status::Failed);
        if (mode == "terminal" && status == SatsArenaRoomClient::Status::Closed) break;
        if (error == "Room server unavailable; reconnecting")
        {
            reconnecting = true;
            assert(status == SatsArenaRoomClient::Status::Waiting && !client->CanNextMatch());
            assert(!client->SetAddress("player@example.test"));
            if (mode == "stop") break;
        }
        if (paid && client->HasPayment())
        {
            const auto payment = client->PaymentView();
            assert(payment["match_id"] == std::string(32, 'a'));
            assert(payment["players"][0]["invoice"] == "unchanged-entry");
        }
        if (reconnecting && status == SatsArenaRoomClient::Status::Started && error.empty())
        { recovered = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (mode == "terminal") assert(client->IsClosed());
    else assert(reconnecting && (mode == "stop" || recovered));
    assert(client->GetRoomCode() == "ABCDEF01" && client->LocalSlot() == 0);
    assert(client->LocalPort() == 53513 && client->GameplayEndpoint() == "127.0.0.1:43513");
    assert(client->LanEndpoint() == "192.168.1.10:53513");
    if (recovered)
    {
        client->ReportPeerPing(1, 194000);
        client->ReportPeerPing(3, 300000);
        assert(client->PeerPings().WorstMilliseconds(0, 2) == 194);
        client->ClearPeerPing(1);
        assert(client->PeerPings().WorstMilliseconds(0, 2) == 0);
        assert(client->PeerPings().players[3].Milliseconds() == 300);
        client->ClearPeerPing();
        assert(client->PeerPings().WorstMilliseconds(0, 4) == 0);
        assert(client->LeaveRoom());
        client->ReportPeerPing(1, 194000);
        assert(!client->PeerPings().active && client->PeerPings().WorstMilliseconds(0, 2) == 0);
    }
    if (mode == "terminal") assert(!client->PeerPings().active);
    const auto before = std::chrono::steady_clock::now();
    client.reset();
    assert(std::chrono::steady_clock::now() - before < std::chrono::seconds(5));
    std::cout << "PASS: room recovery " << mode << " preserves membership and socket context\n";
}
