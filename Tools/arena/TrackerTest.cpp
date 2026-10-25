// Copyright 2026 Sats Arena contributors
// Licensed under GPLv2+
#include "Core/Slippi/SatsArenaTracker.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

using Bytes = std::vector<std::uint8_t>;
using SatsArena::Tracker;

void Check(bool value, const char* message)
{
	if (!value)
		throw std::runtime_error(message);
}

void Word(Bytes& data, std::size_t offset, std::uint32_t value)
{
	for (int i = 0; i < 4; ++i)
		data[offset + i] = static_cast<std::uint8_t>(value >> (24 - i * 8));
}

Bytes Start()
{
	Bytes data(0x1a5);
	data[0] = 0x36;
	data[0x1a4] = 2;
	for (std::size_t i = 0; i < 4; ++i)
		data[0x66 + i * 0x24] = i < 2 ? 0 : 3;
	return data;
}

void Feed(Tracker& tracker, const Bytes& data)
{
	tracker.Process(data.data(), data.size());
}

Bytes Post(int frame, int player, float percent, int last = 0, int stocks = 4, bool follower = false)
{
	Bytes data(0x55);
	data[0] = 0x38;
	Word(data, 1, static_cast<std::uint32_t>(frame));
	data[5] = static_cast<std::uint8_t>(player);
	data[6] = follower;
	std::uint32_t bits;
	std::memcpy(&bits, &percent, sizeof(bits));
	Word(data, 0x16, bits);
	data[0x20] = static_cast<std::uint8_t>(last);
	data[0x21] = static_cast<std::uint8_t>(stocks);
	return data;
}

void Book(Tracker& tracker, int frame, int finalized)
{
	Bytes data(9);
	data[0] = 0x3c;
	Word(data, 1, static_cast<std::uint32_t>(frame));
	Word(data, 5, static_cast<std::uint32_t>(finalized));
	Feed(tracker, data);
}

void Sample(Tracker& tracker, int frame, float p0, float p1)
{
	Feed(tracker, Post(frame, 0, p0, 1));
	Feed(tracker, Post(frame, 1, p1));
}

void BasicAndDuplicate()
{
	Tracker tracker;
	Feed(tracker, Start());
	Sample(tracker, -123, 0, 0);
	Book(tracker, -123, -123);
	Sample(tracker, -122, 3.5f, 8.0f);
	Check(tracker.Read().events.empty(), "speculative damage was published");
	Book(tracker, -122, -122);
	auto state = tracker.Read();
	Check(state.events.size() == 2, "simultaneous damage / negative frame decoding");
	Check(state.events[1].victim == 1 && state.events[1].last_hit_by == 0, "player indices");
	Check(state.events[1].amount == 8 && state.events[0].amount == 3.5f, "float endian decoding");
	Sample(tracker, -122, 99, 99);
	Book(tracker, -122, -122);
	Check(tracker.Read().total_events == 2, "duplicate finalized frame was counted");
}

void Rollback()
{
	Tracker tracker;
	Feed(tracker, Start());
	Sample(tracker, 0, 0, 0);
	Book(tracker, 0, 0);
	Sample(tracker, 1, 0, 10);
	Book(tracker, 1, 0);
	Sample(tracker, 2, 0, 30);
	Sample(tracker, 1, 0, 4);
	Sample(tracker, 2, 0, 9);
	Check(tracker.Read().events.empty(), "rollback prediction leaked");
	Book(tracker, 2, 2);
	auto state = tracker.Read();
	Check(state.events.size() == 2 && state.events[0].amount == 4 && state.events[1].amount == 5,
	      "rollback replacement failed");
}

void InvalidAndReset()
{
	Tracker tracker;
	Feed(tracker, Start());
	Sample(tracker, 0, 0, 0);
	Book(tracker, 0, 0);
	Feed(tracker, Post(1, 1, 100, 0, 4, true));
	Feed(tracker, Post(1, 9, 100));
	Feed(tracker, Post(1, 0, std::numeric_limits<float>::quiet_NaN()));
	Feed(tracker, Post(1, 1, -1));
	Book(tracker, 1, 1);
	Check(tracker.Read().events.empty(), "invalid/follower damage counted");
	Sample(tracker, 2, 0, 10); // Missing observation creates a fresh baseline.
	Book(tracker, 2, 2);
	Check(tracker.Read().events.empty(), "missing frame bridged");
	Feed(tracker, Post(3, 1, 15, 255));
	Book(tracker, 3, 3);
	Check(tracker.Read().events.back().last_hit_by == -1, "unknown source misattributed");
	Feed(tracker, Post(4, 1, 0, 0, 3));
	Book(tracker, 4, 4);
	Check(tracker.Read().total_events == 1, "respawn counted as damage");
	Feed(tracker, Post(5, 1, 5, 1, 3));
	Book(tracker, 5, 5);
	Check(tracker.Read().events.back().last_hit_by == -1, "self damage misattributed");
	Feed(tracker, Post(6, 1, 30));
	Feed(tracker, {0x39});
	Check(tracker.Read().total_events == 2, "game end promoted speculation");
	Feed(tracker, Start());
	Check(tracker.Read().events.empty() && tracker.Read().total_events == 0, "new match not reset");
	tracker.Stop();
	Check(tracker.Read().status == SatsArena::TrackingState::Finished, "stop status");
}

void UnsupportedAndTruncated()
{
	Tracker tracker;
	tracker.Process(nullptr, 0);
	Feed(tracker, {0x36});
	Check(tracker.Read().status == SatsArena::TrackingState::Unsupported, "short header accepted");
	auto start = Start();
	start[0x1a4] = 8;
	Feed(tracker, start);
	Check(tracker.Read().status == SatsArena::TrackingState::Unsupported, "online accepted");
	start = Start(); start[0x0d] = 1;
	Feed(tracker, start);
	Check(tracker.Read().status == SatsArena::TrackingState::Unsupported, "single-side teams accepted");
	start = Start(); start[0x66 + 0x48] = 0;
	Feed(tracker, start);
	Check(tracker.Read().status == SatsArena::TrackingState::LocalMatch, "three-player FFA rejected");
	Feed(tracker, Start());
	Sample(tracker, 0, 0, 0);
	Book(tracker, 0, 0);
	Feed(tracker, {0x38, 1});
	Sample(tracker, 1, 0, 8);
	Feed(tracker, {0x3c, 0, 0, 0, 1});
	Book(tracker, 1, 9);
	Check(tracker.Read().events.empty(), "invalid finalization accepted");
}

void BoundedAndConcurrent()
{
	Tracker tracker;
	Feed(tracker, Start());
	Sample(tracker, 0, 0, 0);
	Book(tracker, 0, 0);
	std::thread reader([&tracker] {
		for (int i = 0; i < 3000; ++i)
			Check(tracker.Read().events.size() <= SatsArena::MAX_EVENTS, "event cap exceeded");
	});
	for (int i = 1; i <= 500; ++i)
	{
		Sample(tracker, i, 0, static_cast<float>(i));
		Book(tracker, i, i);
	}
	reader.join();
	Check(tracker.Read().events.size() == 200 && tracker.Read().total_events == 500, "bounded history");
	Feed(tracker, Start());
	for (int i = 0; i < 3000; ++i)
		Feed(tracker, Post(i, 1, static_cast<float>(i)));
	Book(tracker, 2999, 2999);
	Check(tracker.Read().total_events == SatsArena::MAX_PENDING_FRAMES - 1,
	      "pending cap or discontinuity handling failed");
}

Bytes Credits(int frame, float amount, bool empty = false)
{
	Bytes packet(SatsArena::CREDIT_PACKET_SIZE);
	packet[0] = SatsArena::CREDIT_COMMAND;
	packet[1] = 1;
	packet[2] = empty ? 0 : 1;
	Word(packet, 4, static_cast<std::uint32_t>(frame));
	packet[8] = 0;
	packet[10] = 1;
	Word(packet, 12, 9);
	std::uint32_t bits;
	std::memcpy(&bits, &amount, sizeof(bits));
	Word(packet, 16, bits);
	return packet;
}

void CreditDiagnostics()
{
	Tracker tracker;
	Feed(tracker, Start());
	Sample(tracker, 0, 0, 0);
	Book(tracker, 0, 0);
	Sample(tracker, 1, 0, 4);
	Feed(tracker, Credits(1, 8)); // Nominal credit is intentionally different from the percent delta.
	Check(tracker.Read().total_credit_events == 0, "speculative credit published");
	Book(tracker, 1, 1);
	auto state = tracker.Read();
	Check(state.total_credit_events == 1 && state.total_events == 1, "credit and percent not separate");
	Check(state.events.back().kind == SatsArena::EventKind::GameCredit &&
	      state.events.back().amount == 8 && state.events.back().attack_id == 9 &&
	      state.events.back().attacker == 0, "credit metadata");
	Feed(tracker, Credits(1, 99));
	Book(tracker, 1, 1);
	Check(tracker.Read().total_credit_events == 1, "duplicate credit");
	Sample(tracker, 2, 0, 4);
	Feed(tracker, Credits(2, 12));
	Feed(tracker, Credits(2, 0, true)); // Empty replacement must remove predicted credit.
	Book(tracker, 2, 2);
	Check(tracker.Read().total_credit_events == 1, "empty replacement failed");
	Sample(tracker, 3, 0, 4);
	auto overflow = Credits(3, 5);
	overflow[3] = 1;
	Feed(tracker, overflow);
	Book(tracker, 3, 3);
	Check(tracker.Read().credit_stream_incomplete && tracker.Read().total_credit_events == 1,
	      "overflow should discard credit batch");
	Feed(tracker, Start());
	Sample(tracker, 0, 0, 0);
	Book(tracker, 0, 0);
	Sample(tracker, 1, 0, 0);
	Feed(tracker, Credits(1, 7));
	auto invalid = Credits(1, 8);
	invalid[1] = 99;
	Feed(tracker, invalid);
	Book(tracker, 1, 1);
	Check(tracker.Read().total_credit_events == 0 && tracker.Read().credit_stream_incomplete,
	      "invalid packet retained predicted credit");
	Feed(tracker, Start());
	Sample(tracker, 0, 0, 0);
	Book(tracker, 0, 0);
	Sample(tracker, 1, 0, 0);
	Feed(tracker, Credits(1, 20));
	Sample(tracker, 2, 0, 0);
	Feed(tracker, Credits(2, 30));
	Bytes frame_start(5); frame_start[0] = 0x3a; Word(frame_start, 1, 1);
	Feed(tracker, frame_start);
	Sample(tracker, 1, 0, 0);
	Feed(tracker, Credits(1, 3));
	Sample(tracker, 2, 0, 0);
	auto follower = Credits(2, 4); follower[9] = 1; follower[11] = 1;
	Feed(tracker, follower);
	Book(tracker, 2, 2);
	state = tracker.Read();
	Check(state.total_credit_events == 2 && state.events.front().amount == 3 &&
	      state.events.back().amount == 4 && state.events.back().attacker_follower &&
	      state.events.back().victim_follower, "credit rollback or follower metadata");
	Sample(tracker, 3, 0, 0);
	Feed(tracker, Credits(3, 8));
	Feed(tracker, {0x39});
	Check(tracker.Read().total_credit_events == 2, "end promoted credit");
	Feed(tracker, Start());
	Check(!tracker.Read().credit_stream_seen && !tracker.Read().credit_stream_incomplete &&
	      tracker.Read().total_credit_events == 0, "credit state not reset");
	Feed(tracker, {SatsArena::CREDIT_COMMAND});
	Check(tracker.Read().credit_stream_incomplete, "truncated credit accepted");
}

void RecentCreditHistory()
{
	Tracker tracker;
	Feed(tracker, Start());
	Sample(tracker, 0, 0, 0);
	Book(tracker, 0, 0);
	const int credits = static_cast<int>(SatsArena::MAX_CREDIT_MESSAGES) + 100;
	for (int frame = 1; frame <= credits; ++frame)
	{
		Sample(tracker, frame, 0, static_cast<float>(frame));
		Feed(tracker, Credits(frame, 1));
		Book(tracker, frame, frame);
	}
	const auto state = tracker.Read();
	Check(state.total_credit_events == static_cast<unsigned>(credits) && state.total_events == static_cast<unsigned>(credits),
	      "history eviction changed match totals");
	Check(state.damage_centi[0] == 0 && state.damage_centi[1] == static_cast<unsigned>(credits) * 100,
          "history eviction lost cumulative payment counters");
	Check(state.credit_events.size() == SatsArena::MAX_CREDIT_MESSAGES,
	      "visible credit history is not bounded to its message limit");
	Check(state.credit_events.front().frame == 101 && state.credit_events.back().frame == credits,
	      "ordered handoff did not retain the latest credits in chronological order");
	Check(state.credit_events.front().sequence == 101 && state.credit_events.back().sequence == static_cast<unsigned>(credits),
	      "ring eviction lost sequence continuity/gap detection");
	for (const auto& event : state.credit_events)
		Check(event.kind == SatsArena::EventKind::GameCredit,
		      "unverified percent observation entered the visible log");
	Feed(tracker, Start());
	Check(tracker.Read().credit_events.empty(), "new match retained visible messages");
    Check(tracker.Read().damage_centi[1] == 0, "new match retained payout damage");
	Sample(tracker, 0, 0, 0);
	Book(tracker, 0, 0);
	Sample(tracker, 1, 0, 8);
	Book(tracker, 1, 1);
	Check(tracker.Read().credit_events.empty(), "percent-only damage entered the visible log");
}

Bytes RosterStart(const std::array<std::uint8_t, 4>& kinds, bool teams,
                  const std::array<std::uint8_t, 4>& team_ids)
{
	auto start = Start();
	start[0x0d] = teams;
	for (int i = 0; i < 4; ++i)
	{
		start[0x66 + i * 0x24] = kinds[i];
		start[0x6e + i * 0x24] = team_ids[i];
	}
	return start;
}

Bytes Result(const Bytes& start, int mask, int team = 255, int method = 2)
{
	Bytes result(SatsArena::RESULT_PACKET_SIZE);
	result[0] = SatsArena::RESULT_COMMAND;
	result[1] = 1;
	result[2] = method;
	result[3] = start[0x0d];
	result[5] = mask;
	result[6] = team;
	for (int i = 0; i < 4; ++i)
	{
		result[8 + i] = start[0x66 + i * 0x24];
		const bool active = result[8 + i] < 2;
		if (active) result[4] |= 1u << i;
		result[12 + i] = active && result[3] ? start[0x6e + i * 0x24] : 255;
	}
	return result;
}

void ExpandedRosterAndResults()
{
	Tracker tracker;
	const auto ffa = RosterStart({{0, 0, 0, 0}}, false, {{0, 0, 0, 0}});
	Feed(tracker, ffa);
	Check(tracker.Read().status == SatsArena::TrackingState::LocalMatch &&
	      !tracker.Read().teams && tracker.Read().kinds[3] == 0 && tracker.Read().team_ids[3] == 255,
	      "four-player FFA roster was not captured");
	Feed(tracker, {0x39, 2, 255, 1, 3, 2, 0});
	Check(tracker.Read().winner_port == 3 && tracker.Read().winners[3], "P4 FFA winner lost");
	Feed(tracker, ffa);
	Feed(tracker, {0x39, 1, 255, 1, 3, 0, 2});
	Check(tracker.Read().winner_port == 2, "unambiguous FFA timeout winner lost");
	Feed(tracker, ffa);
	Feed(tracker, {0x39, 2, 255, 0, 1, 255, 2});
	Check(tracker.Read().winner_port < 0, "missing third-player placement invented winner");

	const auto teams = RosterStart({{0, 0, 0, 0}}, true, {{0, 1, 0, 1}});
	Feed(tracker, teams);
	Check(tracker.Read().teams && tracker.Read().team_ids[2] == 0 && tracker.Read().team_ids[3] == 1,
	      "match-start team assignment missing");
	Feed(tracker, Result(teams, 5, 0));
	Check(!tracker.Read().game_ended && tracker.Read().winner_port < 0,
	      "private result was published before standard game end");
	Feed(tracker, {0x39, 2, 255, 0, 2, 3, 1});
	auto state = tracker.Read();
	Check(state.winner_port == 0 && state.winner_team == 0 &&
	      state.winners == std::array<bool, 4>{{true, false, true, false}},
	      "winning team did not include eliminated teammate");

	Feed(tracker, teams);
	Feed(tracker, Result(teams, 5, 0, 1));
	// Individual standings can favor a losing-team player on a timeout; only
	// the private team standings authorize the winning side.
	Feed(tracker, {0x39, 1, 255, 2, 0, 3, 1});
	Check(tracker.Read().winner_team == 0 && tracker.Read().winners[2],
	      "team timeout inferred from individual standings");
	Feed(tracker, teams);
	Feed(tracker, {0x39, 2, 255, 0, 2, 1, 3});
	Check(tracker.Read().winner_port < 0, "team result without team standings invented winner");
	Feed(tracker, teams);
	Feed(tracker, Result(teams, 1, 0));
	Feed(tracker, {0x39, 2, 255, 0, 2, 1, 3});
	Check(tracker.Read().winner_port < 0, "partial winning-team mask accepted");
	Feed(tracker, teams);
	auto wrong_roster = Result(teams, 5, 0); wrong_roster[14] = 1;
	Feed(tracker, wrong_roster);
	Feed(tracker, {0x39, 2, 255, 0, 2, 1, 3});
	Check(tracker.Read().winner_port < 0, "result for different roster accepted");
	Feed(tracker, teams);
	Feed(tracker, Result(teams, 5, 0));
	Feed(tracker, {0x39, 7, 255, 0, 2, 1, 3});
	Check(tracker.Read().winner_port < 0, "no contest accepted earlier winner declaration");
	Feed(tracker, teams);
	Feed(tracker, {SatsArena::RESULT_COMMAND, 1});
	Feed(tracker, {0x39, 2, 255, 0, 2, 1, 3});
	Check(tracker.Read().winner_port < 0, "truncated private result accepted");

	const auto uneven = RosterStart({{0, 0, 0, 1}}, true, {{0, 0, 0, 2}});
	Feed(tracker, uneven);
	Feed(tracker, Result(uneven, 8, 2));
	Feed(tracker, {0x39, 2, 255, 1, 2, 3, 0});
	Check(tracker.Read().winner_team == 2 && tracker.Read().winner_port == 3 &&
	      tracker.Read().kinds[3] == 1 && tracker.Read().winners[3],
	      "CPU-only side victory lost");
	const auto three_teams = RosterStart({{0, 0, 1, 0}}, true, {{0, 1, 2, 0}});
	Feed(tracker, three_teams);
	Feed(tracker, Result(three_teams, 2, 1));
	Feed(tracker, {0x39, 2, 255, 1, 0, 2, 3});
	Check(tracker.Read().winner_team == 1 && tracker.Read().winner_port == 1,
	      "three-team result lost");
	Feed(tracker, RosterStart({{0, 0, 0, 3}}, true, {{0, 0, 0, 255}}));
	Check(tracker.Read().status == SatsArena::TrackingState::Unsupported, "single-side team match accepted");
	Feed(tracker, RosterStart({{0, 0, 0, 3}}, true, {{0, 1, 3, 255}}));
	Check(tracker.Read().status == SatsArena::TrackingState::Unsupported, "invalid in-game team accepted");
}

void OrderedFinancialHandoff()
{
	Tracker tracker;
	const auto roster = RosterStart({{0, 0, 0, 1}}, true, {{0, 1, 0, 1}});
	Feed(tracker, roster);
	for (int player = 0; player < 4; ++player) Feed(tracker, Post(0, player, 0));
	Book(tracker, 0, 0);
	for (int player = 0; player < 4; ++player) Feed(tracker, Post(1, player, 1));
	auto packet = Credits(1, 8);
	packet[2] = 4;
	const std::array<int, 4> attackers = {{0, 1, 3, 1}};
	const std::array<int, 4> victims = {{2, 1, 0, 0}};
	for (int i = 0; i < 4; ++i)
	{
		const auto record = 8 + i * 12;
		packet[record] = attackers[i]; packet[record + 2] = victims[i];
		packet[record + 1] = i == 3; packet[record + 3] = i == 3;
		Word(packet, record + 4, i + 10);
		float amount = 8; std::uint32_t bits;
		std::memcpy(&bits, &amount, 4); Word(packet, record + 8, bits);
	}
	Feed(tracker, packet);
	Book(tracker, 1, 1);
	const auto state = tracker.Read();
	Check(!state.credit_stream_incomplete && state.credit_events.size() == 4,
	      "CPU/self/friendly raw credits were dropped before financial filtering");
	for (int i = 0; i < 4; ++i)
	{
		const auto& event = state.credit_events[i];
		Check(event.sequence == static_cast<unsigned>(i + 1) && event.ordinal == i &&
		      event.attacker == attackers[i] && event.victim == victims[i] && event.attack_id == static_cast<unsigned>(i + 10),
		      "ordered financial handoff lost attribution or sequence");
	}
	Check(state.credit_events.back().attacker_follower && state.credit_events.back().victim_follower,
	      "follower credit ownership was not preserved");
	Feed(tracker, roster);
	Check(tracker.Read().total_credit_events == 0 && tracker.Read().credit_events.empty(),
	      "new match did not reset credit sequence");
}

void InspectReplay(const char* path)
{
	std::ifstream input(path, std::ios::binary);
	Check(input.good(), "cannot open replay");
	Bytes bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	const Bytes prefix = {'{', 'U', 3, 'r', 'a', 'w', '[', '$', 'U', '#', 'l'};
	Check(bytes.size() >= 15 && std::equal(prefix.begin(), prefix.end(), bytes.begin()), "unknown replay header");
	std::map<std::uint8_t, std::size_t> sizes;
	Tracker tracker;
	std::size_t offset = 15;
	while (offset < bytes.size())
	{
		const auto command = bytes[offset];
		if (command == 0x35)
		{
			Check(offset + 1 < bytes.size(), "truncated command list");
			const auto length = bytes[offset + 1];
			Check(length >= 1 && (length - 1) % 3 == 0 && offset + length < bytes.size(), "invalid command list");
			for (std::size_t i = 2; i < length; i += 3)
				sizes[bytes[offset + i]] = (std::size_t(bytes[offset + i + 1]) << 8) | bytes[offset + i + 2];
			offset += length + 1;
			continue;
		}
		const auto size = sizes.find(command);
		if (size == sizes.end() || size->second >= bytes.size() - offset)
			break; // Metadata or the incomplete tail of a live replay.
		tracker.Process(bytes.data() + offset, size->second + 1);
		offset += size->second + 1;
	}
	const auto snapshot = tracker.Read();
	std::cout << "Real replay: " << path << "\nFinalized damage events: " << snapshot.total_events << '\n';
	for (const auto& event : snapshot.events)
		std::cout << "f" << event.frame << " P" << int(event.victim + 1) << " +" << event.amount
		          << "% -> " << event.percent << "% (last-hit hint " << event.last_hit_by << ")\n";
}

int main(int argc, char** argv)
{
	BasicAndDuplicate();
	Rollback();
	InvalidAndReset();
	UnsupportedAndTruncated();
	BoundedAndConcurrent();
	CreditDiagnostics();
	RecentCreditHistory();
	ExpandedRosterAndResults();
	OrderedFinancialHandoff();
	std::cout << "PASS: finalization, rollback, duplicates, resets, invalid data, credits, ordered credit history, bounds, concurrency\n";
	if (argc == 2)
		InspectReplay(argv[1]);
}
