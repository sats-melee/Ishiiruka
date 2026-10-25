#include "Core/Slippi/SatsArenaTracker.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>
static void Check(bool ok, const char* message)
{
	if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
int main()
{
	SatsArena::PaymentGate gate;
	std::array<std::uint8_t, 20> query = {{0xe7,2,0,0,0,0,3,3,1,2,0,0,0,255,255,255,255,0,0,0}};
	Check(!gate.Query(query.data(), query.size()), "not ready");
	Check(!gate.Authorize(gate.Read().generation), "cannot authorize before prompt");
	query[2] = 1;
	Check(!gate.Query(query.data(), query.size()), "ready remains gated");
	const auto ready = gate.Read().generation;
	query[3] = 1;
	Check(!gate.Query(query.data(), query.size()), "unpaid start blocked");
	Check(gate.Read().requested, "start is latched");
	query[3] = 0;
	Check(!gate.Query(query.data(), query.size()), "latched start remains unpaid");
	Check(gate.Authorize(ready), "worker acknowledgement grants current selection");
	Check(gate.Query(query.data(), query.size()), "paid start continues without another press");
	std::this_thread::sleep_for(std::chrono::milliseconds(550));
	Check(!gate.Read().ready, "sidebar hides funding after CSS polling stops");
	Check(!gate.Authorize(ready), "stale scene cannot receive a grant");
	gate.Query(query.data(), query.size());
	query[8] = 4;
	Check(!gate.Query(query.data(), query.size()), "character change revokes admission");
	Check(!gate.Authorize(ready), "stale worker result cannot authorize new selection");
	query[3] = 1;
	gate.Query(query.data(), query.size());
	gate.Reset();
	Check(!gate.Query(query.data(), query.size()), "next match requires fresh authorization");
	query[6] = 0;
	Check(!gate.Query(query.data(), query.size()), "third player blocked");
	Check(!gate.Read().ready, "unsupported roster cannot request funding");
	Check(!gate.Query(query.data(), 11), "truncated query fails closed");
	query[3] = 0;
	query[6] = query[7] = 0;
	query[10] = 3; query[11] = 4;
	query[12] = 1;
	query[13] = 0; query[14] = 1; query[15] = 0; query[16] = 1;
	Check(!gate.Query(query.data(), query.size()), "teams stay blocked pending funding integration");
	auto teams = gate.Read();
	Check(teams.roster_valid && teams.teams && teams.team_ids[2] == 0 && teams.kinds[3] == 0,
	      "four human team roster exposed to UI");
	const auto old_team = teams.generation;
	query[15] = 2;
	gate.Query(query.data(), query.size());
	Check(gate.Read().generation > old_team && gate.Read().team_ids[2] == 2,
	      "team edits invalidate generation and update sidebar data");
	Check(!gate.Authorize(old_team), "old team authorization rejected");
	query[7] = 1;
	gate.Query(query.data(), query.size());
	Check(gate.Read().roster_valid && gate.Read().kinds[3] == 1, "CPU distinguished in team roster");
	const auto previous_teams = query;
	query[13] = query[14] = query[15] = query[16] = 0;
	gate.Query(query.data(), query.size());
	Check(!gate.Read().roster_valid && gate.Read().team_ids[3] == 0,
	      "single-side lobby is blocked while still publishing live colors");
	query = previous_teams;
	query[16] = 3;
	gate.Query(query.data(), query.size());
	Check(!gate.Read().roster_valid, "invalid team ID rejected");
	query[16] = 1;
	query[12] = 0;
	query[13] = query[14] = query[15] = query[16] = 255;
	gate.Query(query.data(), query.size());
	Check(gate.Read().roster_valid && !gate.Read().teams, "four slot FFA exposed");
	query[7] = 3;
	gate.Query(query.data(), query.size());
	Check(gate.Read().roster_valid, "three participant FFA supported as telemetry");
	query[6] = 3;
	query[3] = 1;
	gate.Query(query.data(), query.size());
	Check(gate.Authorize(gate.Read().generation), "valid legacy lobby still authorizes");
	const auto ffa_grant = gate.Read().generation;
	query[12] = 1;
	query[13] = 0; query[14] = 1;
	Check(!gate.Query(query.data(), query.size()) && !gate.Read().authorized,
	      "switching a funded pair to teams revokes FFA admission");
	Check(!gate.Authorize(ffa_grant), "FFA authorization cannot cross into teams");
	query[12] = 0;
	query[13] = query[14] = 255;
	gate.Query(query.data(), query.size());
	Check(gate.Authorize(gate.Read().generation), "new FFA selection can authorize afresh");
	auto reset_query = query;
	reset_query[2] = reset_query[3] = 0;
	for (int i = 4; i < 17; ++i) reset_query[i] = 255;
	reset_query[12] = 0;
	Check(!gate.Query(reset_query.data(), reset_query.size()) && !gate.Read().authorized,
	      "CSS-load v2 reset revokes admission with identical next characters");
	gate.Query(query.data(), query.size());
	Check(gate.Authorize(gate.Read().generation), "new CSS visit can authorize afresh");
	query[1] = 1;
	Check(!gate.Query(query.data(), query.size()) && !gate.Read().authorized,
	      "old protocol revokes prior grant");
	query[1] = 2;
	query[17] = 1;
	Check(!gate.Query(query.data(), query.size()), "reserved bytes rejected");
	query[17] = 0;
	gate.Enable(false);
	query[3] = 0;
	Check(!gate.Query(query.data(), query.size()), "ungated mode does not invent Start input");
	query[3] = 1;
	Check(gate.Query(query.data(), query.size()), "explicit ungated diagnostic mode");

	SatsArena::Tracker tracker;
	std::vector<std::uint8_t> start(0x1a5);
	start[0] = 0x36; start[0x1a4] = 2;
	for (int i = 0; i < 4; ++i) start[0x66 + i * 0x24] = i < 2 ? 0 : 3;
	tracker.Process(start.data(), start.size());
	std::uint8_t end[] = {0x39,2,255,0,1,255,255};
	tracker.Process(end, sizeof(end));
	Check(tracker.Read().game_ended && tracker.Read().winner_port == 0, "P1 normal result");
	tracker.Process(start.data(), start.size());
	end[3] = 1; end[4] = 0;
	tracker.Process(end, sizeof(end));
	Check(tracker.Read().winner_port == 1, "P2 normal result");
	tracker.Process(start.data(), start.size());
	end[3] = 0; end[4] = 0;
	tracker.Process(end, sizeof(end));
	Check(tracker.Read().winner_port == -1, "tied placements do not invent a winner");
	tracker.Process(start.data(), start.size());
	end[3] = 0; end[4] = 255;
	tracker.Process(end, sizeof(end));
	Check(tracker.Read().winner_port == -1, "missing opponent placement has no winner");
	tracker.Process(start.data(), start.size());
	end[1] = 7;
	tracker.Process(end, sizeof(end));
	Check(tracker.Read().winner_port == -1, "LRAS is no contest, no invented winner");
	tracker.Process(start.data(), start.size());
	tracker.Stop();
	Check(!tracker.Read().game_ended && tracker.Read().winner_port == -1, "stop is not a game result");
	std::cout << "PASS: prompt gate, queued Start, stale authorization, v2 roster/teams/CPU, stale-team grants, reset, match results\n";
}
