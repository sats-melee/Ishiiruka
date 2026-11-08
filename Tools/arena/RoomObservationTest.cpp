#include "Core/Slippi/SatsArenaRoomPayments.h"
#include <cassert>
#include <iostream>

int main()
{
    SatsArena::Snapshot match;
    const std::string id(32, 'a');
    match.online = true;
    match.match_id = id;
    match.game_number = 1;
    match.stage_id = 32;
    match.random_seed = 42;
    match.kinds = {{0, 0, 3, 3}};
    match.team_ids.fill(255);
    match.characters = {{17, 17, 0, 0}};
    match.credit_stream_seen = true;
    for (unsigned i = 1; i <= 65; ++i)
    {
        SatsArena::DamageEvent event{static_cast<int>(i), 1, 0, 12.5f, 0};
        event.kind = SatsArena::EventKind::GameCredit;
        event.attacker = 0;
        event.sequence = i;
        match.credit_events.push_back(event);
    }
    match.total_credit_events = 65;
    match.game_ended = match.result_complete = true;
    match.winner_port = 0;
    nlohmann::json command;
    std::string error;
    assert(SatsArenaNetplay::Observation(match, id, 1, command, error));
    assert(command["report"]["credits"].size() == 64);
    assert(command["report"]["end"].is_null());
    assert(command["report"]["manifest"]["characters"][2] == 255);
    assert(command["report"]["credits"][0]["damage_centi"] == 1250);
    assert(SatsArenaNetplay::Observation(match, id, 65, command, error));
    assert(command["report"]["credits"].size() == 1);
    assert(command["report"]["end"]["complete"] == true);
    assert(command["report"]["end"]["winner"] == 0);
    assert(command["report"]["end"]["last_sequence"] == 65);
    match.credit_stream_incomplete = true;
    assert(SatsArenaNetplay::Observation(match, id, 66, command, error));
    assert(command["report"]["end"]["complete"] == false);
    assert(command["report"]["end"]["winner"].is_null());
    match.game_ended = false;
    assert(SatsArenaNetplay::Observation(match, id, 1, command, error));
    assert(command["report"]["end"]["complete"] == false);
    assert(command["report"]["end"]["winner"].is_null());
    match.credit_stream_incomplete = false;
    match.credit_events.pop_front();
    assert(SatsArenaNetplay::Observation(match, id, 1, command, error));
    assert(command["report"]["credits"].empty());
    assert(command["report"]["end"]["complete"] == false);
    assert(!SatsArenaNetplay::Observation(match, id, 0, command, error));
    assert(!SatsArenaNetplay::Observation(match, id, 67, command, error));
    std::cout << "PASS: payment report pagination, exact cents, closed slots, incomplete tails, missing credits, invalid acknowledgements\n";
}
