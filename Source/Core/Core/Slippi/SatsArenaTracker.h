// Copyright 2026 Sats Arena contributors
// Licensed under GPLv2+
// Refer to the license.txt file included.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>

namespace SatsArena
{
constexpr std::size_t MAX_EVENTS = 200;
constexpr std::size_t MAX_CREDIT_MESSAGES = 4096; // Bounded ordered payment handoff.
constexpr std::size_t MAX_PENDING_FRAMES = 300;
constexpr std::uint8_t CREDIT_COMMAND = 0xe6;
constexpr std::uint8_t RESULT_COMMAND = 0xe8;
constexpr std::size_t RESULT_PACKET_SIZE = 16;
constexpr std::size_t MAX_CREDITS_PER_FRAME = 32;
constexpr std::size_t CREDIT_PACKET_SIZE = 8 + 12 * MAX_CREDITS_PER_FRAME;

enum class TrackingState { Waiting, LocalMatch, Unsupported, Finished };
enum class EventKind { PercentChange, GameCredit };

struct DamageEvent
{
	std::int32_t frame;
	std::uint8_t victim;
	int last_hit_by; // A hint, NOT verified attribution of this damage.
	float amount;
	float percent;
	EventKind kind = EventKind::PercentChange;
	int attacker = -1;
	bool attacker_follower = false;
	bool victim_follower = false;
	std::uint32_t attack_id = 0;
	std::uint8_t ordinal = 0;
	std::uint64_t sequence = 0; // Monotonic within the match; zero for percent observations.
};

struct Snapshot
{
	TrackingState status = TrackingState::Waiting;
	std::uint64_t revision = 0;
	std::uint64_t match_number = 0;
	std::uint64_t total_events = 0;
	std::deque<DamageEvent> events;
	std::deque<DamageEvent> credit_events; // Credit-only history for the visible log.
	std::uint64_t total_credit_events = 0;
	std::array<std::uint64_t, 4> damage_centi = {}; // Cumulative finalized attributed credits received.
	bool teams = false;
	std::array<std::uint8_t, 4> kinds = {{255, 255, 255, 255}};
	std::array<std::uint8_t, 4> team_ids = {{255, 255, 255, 255}};
	int winner_port = -1; // Representative of a validated winning side, including CPUs.
	int winner_team = -1;
	std::array<bool, 4> winners = {}; // All winning-side participants, including eliminated teammates.
	bool game_ended = false;
	bool credit_stream_seen = false;
	bool credit_stream_incomplete = false;
};

// Bounded observer of the existing replay protocol. No emulated-memory reads,
// UI callbacks, file I/O, or payments. All published events are finalized.
class Tracker
{
public:
	void Process(const std::uint8_t* data, std::size_t length);
	void Stop();
	Snapshot Read() const;

private:
	struct PlayerFrame
	{
		bool present = false;
		float percent = 0;
		std::uint8_t stocks = 0;
		std::uint8_t last_hit_by = 255;
		std::int32_t frame = -124;
	};
	void Start(const std::uint8_t* data, std::size_t length);
	void Finalize(std::int32_t frame);
	void CreditBatch(const std::uint8_t* data, std::size_t length);
	void Publish(const DamageEvent& event);
	void MarkIncomplete();
	void ResultPacket(const std::uint8_t* data, std::size_t length);
	struct CreditFrame
	{
		bool overflow = false;
		std::deque<DamageEvent> events;
	};
	mutable std::mutex m_mutex;
	Snapshot m_snapshot;
	std::array<bool, 4> m_active = {};
	std::array<PlayerFrame, 4> m_previous = {};
	std::map<std::int32_t, std::array<PlayerFrame, 4>> m_pending;
	std::map<std::int32_t, CreditFrame> m_pending_credits;
	std::int32_t m_finalized = -124;
	std::int32_t m_latest_received = -124;
	bool m_result_seen = false;
	bool m_result_valid = false;
	std::uint8_t m_result_method = 0;
	std::uint8_t m_result_winners = 0;
	int m_result_team = -1;
};

Tracker& GetTracker();
}

namespace SatsArena
{
// A private EXI handshake. No wallet work runs on the emulation thread.
constexpr std::size_t GATE_PACKET_SIZE = 20;
constexpr std::uint8_t GATE_PROTOCOL = 2;
struct GateView
{
	bool roster_valid = false; // Full 2–4 participant telemetry, independent of payment support.
	bool teams = false;
	std::array<std::uint8_t, 4> kinds = {{255, 255, 255, 255}};
	std::array<std::uint8_t, 4> characters = {{255, 255, 255, 255}};
	std::array<std::uint8_t, 4> team_ids = {{255, 255, 255, 255}};
	bool ready = false;
	bool requested = false;
	bool authorized = false;
	std::uint64_t generation = 0;
};
class PaymentGate
{
public:
	bool Query(const std::uint8_t* packet, std::size_t length);
	GateView Read() const;
	bool Authorize(std::uint64_t generation);
	void Reset();
	void Enable(bool enabled);
private:
	mutable std::mutex m_mutex;
	GateView m_view;
	std::array<std::uint8_t, 13> m_selection = {};
	std::chrono::steady_clock::time_point m_last_query = {};
	bool m_enabled = true; // Missing UI/engine fails closed.
};
PaymentGate& GetPaymentGate();
}
