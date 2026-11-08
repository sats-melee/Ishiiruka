#pragma once

#include <algorithm>
#include <cstring>
#include <string>

#include "Core/Slippi/SatsArenaRoomPayments.h"
#include "sats_arena_local.h"

namespace SatsArenaLocal
{
// Copy only a validated server snapshot; failed snapshots clear every invoice.
inline bool RoomView(const nlohmann::json& payment, ArenaLocalView& view)
{
    view = {};
    if (!SatsArenaNetplay::Payment(payment) || !payment["phase"].is_number_integer() ||
        !payment.count("mode") || !payment["mode"].is_string() ||
        payment["invoice_sats"] < payment["entry_sats"] ||
        !SatsArenaNetplay::Amount(payment["surcharge_bps"], 10000)) return false;
    const auto mode = payment["mode"].get<std::string>();
    if (mode != "sandbox" && mode != "nwc" && mode != "fake" && mode != "simulated") return false;
    auto copy = [](const nlohmann::json& text, char* out, size_t capacity, bool truncate = false) {
        if (!text.is_string()) return false;
        const auto value = text.get<std::string>();
        if (value.find('\0') != std::string::npos || (!truncate && value.size() >= capacity)) return false;
        std::memcpy(out, value.data(), std::min(value.size(), capacity - 1));
        return true;
    };
    ArenaLocalView next = {};
    next.abi_version = 13;
    next.phase = payment["frozen"].get<bool>() ? 7 : payment["phase"].get<unsigned>();
    next.sandbox = mode == "sandbox";
    next.simulated = mode == "fake" || mode == "simulated";
    next.entry_sats = payment["entry_sats"].get<std::uint64_t>();
    next.invoice_sats = payment["invoice_sats"].get<std::uint64_t>();
    next.surcharge_sats = next.invoice_sats - next.entry_sats;
    next.sats_per_percent = payment["sats_per_percent"].get<std::uint64_t>();
    next.batch_window_ms = payment["batch_window_ms"].get<std::uint64_t>();
    next.surcharge_bps = payment["surcharge_bps"].get<unsigned>();
    next.teams = payment["teams"].get<bool>();
    next.roster_configured = 1;
    if (payment.count("error") && !payment["error"].is_null() &&
        !copy(payment["error"], next.error, sizeof(next.error), true)) return false;
    if (next.phase == 7 && !next.error[0])
        std::strcpy(next.error, "Payments frozen by server; settlement needs review.");
    if (payment.count("payment_log") &&
        !copy(payment["payment_log"], next.payment_log, sizeof(next.payment_log), true)) return false;
    for (size_t i = 0; i < 4; ++i)
    {
        const auto& player = payment["players"][i];
        if (!copy(player["address"], next.addresses[i], sizeof(next.addresses[i])) ||
            !copy(player["invoice"], next.invoices[i], sizeof(next.invoices[i]))) return false;
        next.kinds[i] = payment["kinds"][i].get<unsigned>();
        next.team_ids[i] = payment["team_ids"][i].get<unsigned>();
        next.characters[i] = payment["characters"][i].get<unsigned>();
        next.paid_sats[i] = player["paid_sats"].get<std::uint64_t>();
        next.remaining_sats[i] = player["remaining_sats"].get<std::uint64_t>();
        next.earned_sats[i] = player["earned_sats"].get<std::uint64_t>();
        next.pending_sats[i] = player["pending_sats"].get<std::uint64_t>();
        next.batch_remaining_ms[i] = payment["batch_remaining_ms"][i].get<std::uint64_t>();
        const bool verified = player["verified"].get<bool>();
        next.verification_state[i] = player.count("verification_state") ?
            player["verification_state"].get<unsigned>() : (verified ? 2 : 0);
        // An invoice is usable only after the server confirms the receiver.
        if (next.verification_state[i] == 2 && !verified) return false;
        if (player.count("verification_message") &&
            !copy(player["verification_message"], next.verification_message[i],
                  sizeof(next.verification_message[i]), true)) return false;
    }
    view = next;
    return true;
}
}
