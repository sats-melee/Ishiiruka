// Copyright 2026 Sats Arena contributors
// Licensed under GPLv2+
// Refer to the license.txt file included.
#include "Core/Slippi/SatsArenaTracker.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace SatsArena
{
namespace
{
std::uint32_t Word(const std::uint8_t* data)
{
	return (std::uint32_t(data[0]) << 24) | (std::uint32_t(data[1]) << 16) |
	       (std::uint32_t(data[2]) << 8) | std::uint32_t(data[3]);
}

std::int32_t Frame(const std::uint8_t* data)
{
	const std::uint32_t bits = Word(data);
	std::int32_t value;
	std::memcpy(&value, &bits, sizeof(value));
	return value;
}

float Percent(const std::uint8_t* data)
{
	const std::uint32_t bits = Word(data);
	float value;
	std::memcpy(&value, &bits, sizeof(value));
	return value;
}
}

Tracker& GetTracker()
{
	static Tracker tracker;
	return tracker;
}

void Tracker::Start(const std::uint8_t* data, std::size_t length)
{
	GetPaymentGate().Reset();
	++m_snapshot.match_number;
	m_snapshot.winner_port = -1;
	m_snapshot.winner_team = -1;
	m_snapshot.winners = {};
	m_snapshot.teams = false;
	m_snapshot.kinds.fill(255);
	m_snapshot.team_ids.fill(255);
	m_result_seen = m_result_valid = false;
	m_result_method = m_result_winners = 0;
	m_result_team = -1;
	m_snapshot.game_ended = false;
	m_snapshot.status = TrackingState::Unsupported;
	m_snapshot.events.clear();
	m_snapshot.credit_events.clear();
	m_snapshot.total_events = 0;
	m_snapshot.total_credit_events = 0;
	m_snapshot.damage_centi = {};
	m_snapshot.credit_stream_seen = false;
	m_snapshot.credit_stream_incomplete = false;
	++m_snapshot.revision;
	m_previous = {};
	m_active = {};
	m_pending.clear();
	m_pending_credits.clear();
	m_finalized = m_latest_received = -124;
	// Offsets include the command byte. Modern local VS setup carries all four
	// slot kinds and the authoritative match-start teams, independently of CSS.
	if (length <= 0x1a4 || data[0x1a4] != 2 || data[0x0d] > 1)
		return;
	m_snapshot.teams = data[0x0d] != 0;
	int players = 0;
	unsigned sides = 0;
	for (std::size_t i = 0; i < 4; ++i)
	{
		const auto type = data[0x66 + 0x24 * i];
		if (type != 0 && type != 1 && type != 3)
			return;
		m_snapshot.kinds[i] = type;
		m_active[i] = type < 2;
		if (!m_active[i]) continue;
		++players;
		if (m_snapshot.teams)
		{
			const auto team = data[0x6e + 0x24 * i];
			if (team > 2) return;
			m_snapshot.team_ids[i] = team;
			sides |= 1u << team;
		}
	}
	if (players >= 2 && (!m_snapshot.teams || (sides && (sides & (sides - 1)))))
		m_snapshot.status = TrackingState::LocalMatch;
}

void Tracker::Process(const std::uint8_t* data, std::size_t length)
{
	if (!data || length == 0 || (data[0] != 0x36 && data[0] != 0x38 && data[0] != 0x39 &&
	                           data[0] != 0x3a && data[0] != 0x3c && data[0] != CREDIT_COMMAND &&
	                           data[0] != RESULT_COMMAND))
		return;
	std::lock_guard<std::mutex> lock(m_mutex);
	if (data[0] == 0x36)
	{
		Start(data, length);
		return;
	}
	if (m_snapshot.status != TrackingState::LocalMatch)
		return;
	if (data[0] == 0x39)
	{
		m_snapshot.status = TrackingState::Finished;
		m_snapshot.game_ended = true;
		// The private packet carries actual team standings. Individual replay
		// placements cannot establish a team winner (notably on timeouts).
		if (length >= 7 && m_result_seen)
		{
			if (m_result_valid && data[1] == m_result_method)
			{
				m_snapshot.winner_team = m_result_team;
				for (int i = 0; i < 4; ++i)
				{
					m_snapshot.winners[i] = (m_result_winners & (1u << i)) != 0;
					if (m_snapshot.winners[i] && m_snapshot.winner_port < 0)
						m_snapshot.winner_port = i;
				}
			}
		}
		else if (length >= 7 && !m_snapshot.teams && (data[1] == 1 || data[1] == 2 || data[1] == 3))
		{
			int candidate = -1;
			int winners = 0;
			bool complete = true;
			const auto participants = std::count(m_active.begin(), m_active.end(), true);
			for (int i = 0; i < 4; ++i)
			{
				if (!m_active[i]) { complete = complete && data[3 + i] == 255; continue; }
				complete = complete && data[3 + i] < participants;
				if (data[3 + i] == 0) { candidate = i; ++winners; }
			}
			if (complete && winners == 1)
			{
				m_snapshot.winner_port = candidate;
				m_snapshot.winners[candidate] = true;
			}
		}
		GetPaymentGate().Reset();
		m_pending.clear(); // Never promote speculative frames at game end.
		m_pending_credits.clear();
		++m_snapshot.revision;
		return;
	}
	if (data[0] == RESULT_COMMAND)
	{
		ResultPacket(data, length);
		return;
	}
	if (data[0] == CREDIT_COMMAND)
	{
		CreditBatch(data, length);
		return;
	}
	if (data[0] == 0x3a && length >= 5)
	{
		const auto frame = Frame(data + 1);
		if (frame <= m_finalized)
			return;
		if (frame <= m_latest_received)
		{
			m_pending.erase(m_pending.lower_bound(frame), m_pending.end());
			m_pending_credits.erase(m_pending_credits.lower_bound(frame), m_pending_credits.end());
		}
		m_latest_received = frame;
	}
	else if (data[0] == 0x38)
	{
		if (length < 0x22 || data[5] >= 4 || data[6] != 0 || !m_active[data[5]])
			return; // Ignore Nana/followers rather than double-counting a player's damage.
		const auto frame = Frame(data + 1);
		const float percent = Percent(data + 0x16);
		if (frame <= m_finalized || !std::isfinite(percent) || percent < 0 || percent > 9999)
			return;
		if (frame < m_latest_received)
		{
			m_pending.erase(m_pending.lower_bound(frame), m_pending.end());
			m_pending_credits.erase(m_pending_credits.lower_bound(frame), m_pending_credits.end());
		}
		m_latest_received = frame;
		PlayerFrame player;
		player.present = true;
		player.percent = percent;
		player.stocks = data[0x21];
		player.last_hit_by = data[0x20];
		player.frame = frame;
		m_pending[frame][data[5]] = player;
		while (m_pending.size() > MAX_PENDING_FRAMES)
		{
			m_pending_credits.erase(m_pending.begin()->first);
			m_pending.erase(m_pending.begin());
		}
	}
	else if (data[0] == 0x3c && length >= 9)
	{
		const auto frame = Frame(data + 1);
		const auto finalized = Frame(data + 5);
		if (finalized <= frame && finalized > m_finalized)
			Finalize(finalized);
	}
}

void Tracker::MarkIncomplete()
{
	if (!m_snapshot.credit_stream_incomplete)
	{
		m_snapshot.credit_stream_incomplete = true;
		++m_snapshot.revision;
	}
}

void Tracker::Publish(const DamageEvent& observation)
{
	auto event = observation;
	if (event.kind == EventKind::GameCredit)
	{
		event.sequence = ++m_snapshot.total_credit_events;
        // Independent of the bounded visible history: UI polling cannot lose payouts.
        m_snapshot.damage_centi[event.victim] +=
            static_cast<std::uint64_t>(std::llround(double(event.amount) * 100.0));
		m_snapshot.credit_events.push_back(event);
		if (m_snapshot.credit_events.size() > MAX_CREDIT_MESSAGES)
			m_snapshot.credit_events.pop_front();
	}
	else
		++m_snapshot.total_events;
	m_snapshot.events.push_back(event);
	++m_snapshot.revision;
	if (m_snapshot.events.size() > MAX_EVENTS)
		m_snapshot.events.pop_front();
}

void Tracker::ResultPacket(const std::uint8_t* data, std::size_t length)
{
	m_result_seen = true;
	m_result_valid = false;
	m_result_winners = 0;
	m_result_team = -1;
	if (length != RESULT_PACKET_SIZE || data[1] != 1 || data[3] > 1 || data[7] != 0 ||
		(data[3] != 0) != m_snapshot.teams)
		return;
	std::uint8_t active = 0;
	for (int i = 0; i < 4; ++i)
	{
		if (m_active[i]) active |= 1u << i;
		if (data[8 + i] != m_snapshot.kinds[i] || data[12 + i] != m_snapshot.team_ids[i])
			return; // A result cannot settle a different funded/match-start roster.
	}
	if (data[4] != active || !data[5] || (data[5] & ~active) ||
		(data[2] != 1 && data[2] != 2 && data[2] != 3))
		return;
	if (m_snapshot.teams)
	{
		if (data[6] > 2) return;
		std::uint8_t expected = 0;
		for (int i = 0; i < 4; ++i)
			if (m_active[i] && m_snapshot.team_ids[i] == data[6]) expected |= 1u << i;
		if (!expected || expected != data[5]) return;
		m_result_team = data[6];
	}
	else if (data[6] != 255 || (data[5] & (data[5] - 1)))
		return;
	m_result_method = data[2];
	m_result_winners = data[5];
	m_result_valid = true;
}

void Tracker::CreditBatch(const std::uint8_t* data, std::size_t length)
{
	if (length != CREDIT_PACKET_SIZE || data[1] != 1 || data[2] > MAX_CREDITS_PER_FRAME || data[3] > 1)
	{
		if (length >= 8)
			m_pending_credits.erase(Frame(data + 4));
		MarkIncomplete();
		return;
	}
	const auto frame = Frame(data + 4);
	if (frame <= m_finalized)
		return;
	CreditFrame batch;
	batch.overflow = data[3] != 0;
	for (std::size_t i = 0; i < data[2]; ++i)
	{
		const auto* record = data + 8 + 12 * i;
		const auto attacker = record[0];
		const auto victim = record[2];
		const auto amount = Percent(record + 8);
		if (attacker >= 4 || victim >= 4 || record[1] > 1 || record[3] > 1 ||
		    !m_active[attacker] || !m_active[victim] || !std::isfinite(amount) || amount < 0 || amount > 9999)
		{
			m_pending_credits.erase(frame);
			MarkIncomplete();
			return; // Reject the whole packet, not a misleading partial interpretation.
		}
		if (amount == 0)
			continue;
		DamageEvent event{frame, victim, -1, amount, 0};
		event.kind = EventKind::GameCredit;
		event.attacker = attacker;
		event.attacker_follower = record[1] != 0;
		event.victim_follower = record[3] != 0;
		event.attack_id = Word(record + 4);
		event.ordinal = static_cast<std::uint8_t>(i);
		batch.events.push_back(event);
	}
	if (!m_snapshot.credit_stream_seen)
	{
		m_snapshot.credit_stream_seen = true;
		++m_snapshot.revision;
	}
	m_pending_credits[frame] = std::move(batch); // Empty batches replace predictions too.
	while (m_pending_credits.size() > MAX_PENDING_FRAMES)
	{
		m_pending_credits.erase(m_pending_credits.begin());
		MarkIncomplete();
	}
}

void Tracker::Finalize(std::int32_t frame)
{
	for (auto it = m_pending.begin(); it != m_pending.end() && it->first <= frame;)
	{
		for (std::size_t i = 0; i < 4; ++i)
		{
			const auto& current = it->second[i];
			auto& previous = m_previous[i];
			if (!current.present)
			{
				previous.present = false;
				continue;
			}
			// A reset, missing frame, stock loss, or first observation sets a fresh
			// baseline. Never turn an unknown gap or a respawn into a hit.
			if (previous.present && current.frame == previous.frame + 1 &&
			    current.stocks == previous.stocks && current.stocks > 0 &&
			    current.percent - previous.percent > 0.00001f)
			{
				const auto last = current.last_hit_by;
				const int hint = last < 4 && last != i && m_active[last] ? last : -1;
				Publish({current.frame, static_cast<std::uint8_t>(i), hint,
				         current.percent - previous.percent, current.percent});
			}
			previous = current;
		}
		const auto credit = m_pending_credits.find(it->first);
		if (credit != m_pending_credits.end())
		{
			if (credit->second.overflow)
				MarkIncomplete(); // Drop overflowed batches rather than claim complete capture.
			else
				for (const auto& event : credit->second.events)
					Publish(event);
			m_pending_credits.erase(credit);
		}
		it = m_pending.erase(it);
	}
	while (!m_pending_credits.empty() && m_pending_credits.begin()->first <= frame)
	{
		m_pending_credits.erase(m_pending_credits.begin());
		MarkIncomplete(); // No ordinary frame data corroborated this batch's frame.
	}
	m_finalized = frame;
}

void Tracker::Stop()
{
	GetPaymentGate().Reset();
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_snapshot.status == TrackingState::LocalMatch)
	{
		m_snapshot.status = TrackingState::Finished;
		++m_snapshot.revision;
	}
	m_pending.clear();
	m_pending_credits.clear();
}

Snapshot Tracker::Read() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_snapshot;
}
}

namespace SatsArena
{
PaymentGate& GetPaymentGate()
{
	static PaymentGate gate;
	return gate;
}
bool PaymentGate::Query(const std::uint8_t* packet, std::size_t length)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	auto invalidate = [this]() {
		const auto next = m_view.generation + 1;
		const auto next_lobby = m_view.lobby_generation + 1;
		m_view = {};
		m_view.generation = next;
		m_view.lobby_generation = next_lobby;
		m_selection = {};
		return false;
	};
	if (!packet || length != GATE_PACKET_SIZE || packet[0] != 0xe7 || packet[1] != GATE_PROTOCOL ||
		packet[2] > 1 || packet[3] > 1 || packet[12] > 1 ||
		packet[17] || packet[18] || packet[19])
		return invalidate();
	const auto now = std::chrono::steady_clock::now();
	if (m_last_query != std::chrono::steady_clock::time_point{} &&
		now - m_last_query > std::chrono::milliseconds(500))
	{
		++m_view.generation;
		m_view.requested = false;
		m_view.authorized = false;
	}
	m_last_query = now;
	bool roster_valid = true;
	int participants = 0;
	unsigned sides = 0;
	for (int i = 0; i < 4; ++i)
	{
		const auto kind = packet[4 + i];
		const auto team = packet[13 + i];
		if (kind != 0 && kind != 1 && kind != 3) roster_valid = false;
		if (kind < 2)
		{
			++participants;
			if (packet[8 + i] >= 25) roster_valid = false;
			if (packet[12])
			{
				if (team > 2) roster_valid = false;
				else sides |= 1u << team;
			}
			else if (team != 255) roster_valid = false;
		}
		else if (team != 255) roster_valid = false;
	}
	roster_valid = roster_valid && participants >= 2 && participants <= 4 &&
		(!packet[12] || (sides && (sides & (sides - 1))));
	const bool ready = packet[2] && roster_valid;
	std::array<std::uint8_t, 13> selection;
	std::copy(packet + 4, packet + 17, selection.begin());
	if (selection != m_selection) ++m_view.lobby_generation;
	if (ready != m_view.ready || roster_valid != m_view.roster_valid || selection != m_selection)
	{
		++m_view.generation;
		m_view.requested = false;
		m_view.authorized = false;
		m_selection = selection;
	}
	m_view.ready = ready;
	m_view.roster_valid = roster_valid;
	m_view.teams = packet[12] != 0;
	for (int i = 0; i < 4; ++i)
	{
		m_view.kinds[i] = packet[4 + i];
		m_view.characters[i] = packet[8 + i];
		m_view.team_ids[i] = packet[13 + i];
	}
	if (!m_enabled) return packet[3] != 0;
	if (ready && packet[3])
		m_view.requested = true;
	return ready && m_view.requested && m_view.authorized;
}
GateView PaymentGate::Read() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	auto view = m_view;
	if (std::chrono::steady_clock::now() - m_last_query > std::chrono::milliseconds(500))
	{
		view.ready = false;
		view.roster_valid = false;
		view.kinds.fill(255);
		view.requested = false;
		view.authorized = false;
	}
	return view;
}
bool PaymentGate::Authorize(std::uint64_t generation)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_view.ready || !m_view.requested || generation != m_view.generation ||
		std::chrono::steady_clock::now() - m_last_query > std::chrono::milliseconds(500))
		return false;
	m_view.authorized = true;
	return true;
}
void PaymentGate::Reset()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const auto next = m_view.generation + 1;
	const auto next_lobby = m_view.lobby_generation + 1;
	m_view = {};
	m_view.generation = next;
	m_view.lobby_generation = next_lobby;
	m_selection = {};
}
void PaymentGate::Enable(bool enabled)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_enabled = enabled;
}
}
