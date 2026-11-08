#pragma once

#include <cmath>
#include <string>
#include <json.hpp>
#include "Core/Slippi/SatsArenaTracker.h"
#include "Core/Slippi/SatsArenaNetplayRoom.h"

namespace SatsArenaNetplay
{
inline bool Amount(const nlohmann::json& value, std::uint64_t maximum = 2100000000000000ULL)
{
    return value.is_number_integer() && value >= 0 && value <= maximum;
}

inline bool Payment(const nlohmann::json& value)
{
    std::string id;
    if (!Text(value, "match_id", id, 32) || id.size() != 32 ||
        id.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos ||
        !value.count("phase") || !(value["phase"] == 1 || value["phase"] == 2 ||
        value["phase"] == 3 || value["phase"] == 4 || value["phase"] == 6) ||
        !value.count("frozen") || !value["frozen"].is_boolean() ||
        !value.count("teams") || !value["teams"].is_boolean()) return false;
    for (const auto key : {"entry_sats", "invoice_sats", "sats_per_percent", "batch_window_ms",
                           "surcharge_bps", "agreed_sequence"})
        if (!value.count(key) || !Amount(value[key])) return false;
    for (const auto key : {"kinds", "team_ids", "characters", "batch_remaining_ms", "players"})
        if (!value.count(key) || !value[key].is_array() || value[key].size() != 4) return false;
    if (!value.count("own_next_sequence") || !value["own_next_sequence"].is_array() ||
        value["own_next_sequence"].size() != 4) return false;
    for (const auto& sequence : value["own_next_sequence"])
        if (!Amount(sequence) || sequence == 0) return false;
    if (value.count("error") && !value["error"].is_null() &&
        (!value["error"].is_string() || value["error"].get<std::string>().size() > 256)) return false;
    for (size_t i = 0; i < 4; ++i)
    {
        if (!Amount(value["kinds"][i], 3) || !Amount(value["team_ids"][i], 255) ||
            !Amount(value["characters"][i], 255) || !Amount(value["batch_remaining_ms"][i])) return false;
        const auto& player = value["players"][i];
        if (!player.is_object()) return false;
        for (const auto key : {"address", "invoice"})
            if (!player.count(key) || !player[key].is_string() ||
                player[key].get<std::string>().size() > (std::string(key) == "address" ? 127 : 2047)) return false;
        if (!player.count("verified") || !player["verified"].is_boolean()) return false;
        for (const auto key : {"paid_sats", "remaining_sats", "earned_sats", "pending_sats"})
            if (!player.count(key) || !Amount(player[key])) return false;
        if (player.count("verification_state") && !Amount(player["verification_state"], 5)) return false;
        if (player.count("verification_message") && (!player["verification_message"].is_string() ||
            player["verification_message"].get<std::string>().size() > 256)) return false;
    }
    return true;
}

// Only the tracker bookend can publish events to this wire format.
inline bool Observation(const SatsArena::Snapshot& match, const std::string& id,
                        std::uint64_t next, nlohmann::json& command, std::string& error)
{
    if (!match.online || !next || next > match.total_credit_events + 1)
    {
        error = "Invalid payment report acknowledgement";
        return false;
    }
    auto characters = match.characters;
    for (size_t i = 0; i < 4; ++i)
        if (match.kinds[i] == 3) characters[i] = 255;
    nlohmann::json manifest = {{"game_number", match.game_number}, {"stage", match.stage_id},
        {"seed", match.random_seed}, {"teams", match.teams}, {"kinds", match.kinds},
        {"team_ids", match.team_ids}, {"characters", characters}};
    auto credits = nlohmann::json::array();
    bool missing = match.match_id != id || match.tiebreak_number != 0 || match.credit_stream_incomplete;
    std::uint64_t expected = next;
    for (const auto& event : match.credit_events)
    {
        if (event.sequence < next) continue;
        if (credits.size() == 64) break;
        if (event.sequence != expected || event.attacker < 0 || event.attacker > 3 ||
            event.victim > 3 || !std::isfinite(event.amount) || event.amount < 0 || event.amount > 9999)
        {
            missing = true;
            break;
        }
        credits.push_back({{"sequence", event.sequence}, {"frame", event.frame},
            {"attacker", event.attacker}, {"victim", event.victim},
            {"damage_centi", static_cast<std::uint64_t>(std::llround(event.amount * 100.0))}});
        ++expected;
    }
    if (next <= match.total_credit_events && credits.empty()) missing = true;
    nlohmann::json end = nullptr;
    // A missing prefix freezes funds; it never silently skips credits or invents a winner.
    if (missing || (match.game_ended && expected > match.total_credit_events))
    {
        const bool complete = !missing && match.result_complete && match.credit_stream_seen &&
                              !match.credit_stream_incomplete;
        end = {{"last_sequence", match.total_credit_events}, {"winner", nullptr}, {"complete", complete}};
        if (complete && match.winner_port >= 0 && match.winner_port < 4)
            end["winner"] = match.winner_port;
    }
    command = {{"type", "report"}, {"report", {{"match_id", id}, {"manifest", manifest},
               {"credits", credits}, {"end", end}}}};
    error.clear();
    return true;
}
}
