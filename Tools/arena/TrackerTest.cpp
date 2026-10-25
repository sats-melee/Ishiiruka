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
	Check(tracker.Read().status == SatsArena::TrackingState::Unsupported, "teams accepted");
	start = Start(); start[0x66 + 0x48] = 0;
	Feed(tracker, start);
	Check(tracker.Read().status == SatsArena::TrackingState::Unsupported, "three players accepted");
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
	for (int frame = 1; frame <= 450; ++frame)
	{
		Sample(tracker, frame, 0, static_cast<float>(frame));
		Feed(tracker, Credits(frame, 1));
		Book(tracker, frame, frame);
	}
	const auto state = tracker.Read();
	Check(state.total_credit_events == 450 && state.total_events == 450,
	      "history eviction changed match totals");
	Check(state.damage_centi[0] == 0 && state.damage_centi[1] == 45000,
          "history eviction lost cumulative payment counters");
	Check(state.credit_events.size() == SatsArena::MAX_CREDIT_MESSAGES,
	      "visible credit history is not bounded to its message limit");
	Check(state.credit_events.front().frame == 351 && state.credit_events.back().frame == 450,
	      "visible history did not retain the latest credits in chronological order");
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
	std::cout << "PASS: finalization, rollback, duplicates, resets, invalid data, credits, last-100 history, bounds, concurrency\n";
	if (argc == 2)
		InspectReplay(argv[1]);
}
