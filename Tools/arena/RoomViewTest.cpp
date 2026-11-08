#include "DolphinWX/SatsArenaRoomView.h"

#include <cassert>
#include <iostream>

int main()
{
    nlohmann::json payment = {
        {"match_id", std::string(32, 'a')}, {"mode", "sandbox"}, {"phase", 1}, {"frozen", false},
        {"teams", false}, {"entry_sats", 100}, {"invoice_sats", 103}, {"sats_per_percent", 1},
        {"batch_window_ms", 5000}, {"surcharge_bps", 300}, {"agreed_sequence", 0},
        {"own_next_sequence", {1, 1, 1, 1}}, {"kinds", {0, 0, 3, 3}}, {"team_ids", {0, 1, 0, 0}},
        {"characters", {2, 20, 255, 255}}, {"batch_remaining_ms", {1200, 0, 0, 0}},
        {"error", nullptr}, {"payment_log", "Entry confirmed"}
    };
    const nlohmann::json player = {{"address", "player@example.test"}, {"invoice", "lntb-test"},
        {"verified", true}, {"verification_state", 2}, {"verification_message", "Receiver checked"},
        {"paid_sats", 103}, {"remaining_sats", 90}, {"earned_sats", 7}, {"pending_sats", 3}};
    payment["players"] = nlohmann::json::array({player, player, player, player});
    ArenaLocalView view = {};
    assert(SatsArenaLocal::RoomView(payment, view));
    assert(view.phase == 1 && view.sandbox && !view.simulated && view.surcharge_sats == 3);
    assert(view.kinds[2] == 3 && view.characters[1] == 20 && view.batch_remaining_ms[0] == 1200);
    assert(view.paid_sats[0] == 103 && view.remaining_sats[0] == 90 && view.earned_sats[0] == 7 && view.pending_sats[0] == 3);
    assert(std::string(view.invoices[0]) == "lntb-test" && std::string(view.payment_log) == "Entry confirmed");
    auto rejects = [&](nlohmann::json bad) {
        std::strcpy(view.invoices[0], "old-payable-invoice");
        assert(!SatsArenaLocal::RoomView(bad, view));
        assert(!view.invoices[0][0] && !view.addresses[0][0] && !view.phase);
    };
    auto bad = payment;
    bad["players"][0]["invoice"] = std::string(2048, 'a');
    rejects(bad);
    bad = payment; bad["players"][0]["address"] = std::string("bad\0address", 11); rejects(bad);
    bad = payment; bad["players"][0]["verified"] = false; rejects(bad);
    bad = payment; bad["players"][0]["pending_sats"] = -1; rejects(bad);
    bad = payment; bad["invoice_sats"] = 99; rejects(bad);
    bad = payment; bad["mode"] = 123; rejects(bad);
    bad = payment; bad["mode"] = "unknown"; rejects(bad);
    bad = payment; bad["payment_log"] = false; rejects(bad);
    rejects(nullptr);
    payment["frozen"] = true;
    assert(SatsArenaLocal::RoomView(payment, view) && view.phase == 7 && view.error[0]);
    payment["frozen"] = false;
    payment["mode"] = "nwc";
    payment["phase"] = 6;
    assert(SatsArenaLocal::RoomView(payment, view) && view.phase == 6 && !view.sandbox && !view.simulated);
    payment["error"] = "Server disagreement";
    assert(SatsArenaLocal::RoomView(payment, view) && std::string(view.error) == "Server disagreement");
    payment["players"][0]["invoice"] = std::string(2047, 'x');
    assert(SatsArenaLocal::RoomView(payment, view) && std::strlen(view.invoices[0]) == 2047);
    std::cout << "Room funding view tests passed\n";
}
