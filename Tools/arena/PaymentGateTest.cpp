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
	std::array<std::uint8_t, 12> query = {{0xe7,1,0,0,0,0,3,3,1,2,0,0}};
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
	std::cout << "PASS: prompt gate, queued Start, stale authorization, roster, reset, match results\n";
}
