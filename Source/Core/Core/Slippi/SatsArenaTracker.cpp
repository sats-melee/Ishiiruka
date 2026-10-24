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
	m_snapshot.status = TrackingState::Unsupported;
	m_snapshot.events.clear();
	m_snapshot.credit_events.clear();
	m_snapshot.total_events = 0;
	m_snapshot.total_credit_events = 0;
	m_snapshot.credit_stream_seen = false;
	m_snapshot.credit_stream_incomplete = false;
	++m_snapshot.revision;
	m_previous = {};
	m_active = {};
	m_pending.clear();
	m_pending_credits.clear();
	m_finalized = m_latest_received = -124;
	// Offsets include the command byte. Only modern, local, non-team VS games
	// with exactly two human/CPU participants are supported in this first version.
	if (length <= 0x1a4 || data[0x1a4] != 2 || data[0x0d] != 0)
		return;
	int players = 0;
	for (std::size_t i = 0; i < 4; ++i)
	{
		const auto type = data[0x66 + 0x24 * i];
		if (type == 2 || type > 3)
			return;
		m_active[i] = type < 2;
		players += m_active[i] ? 1 : 0;
	}
	if (players == 2)
		m_snapshot.status = TrackingState::LocalMatch;
}

void Tracker::Process(const std::uint8_t* data, std::size_t length)
{
	if (!data || length == 0 || (data[0] != 0x36 && data[0] != 0x38 && data[0] != 0x39 &&
	                           data[0] != 0x3a && data[0] != 0x3c && data[0] != CREDIT_COMMAND))
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
		m_pending.clear(); // Never promote speculative frames at game end.
		m_pending_credits.clear();
		++m_snapshot.revision;
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

void Tracker::Publish(const DamageEvent& event)
{
	m_snapshot.events.push_back(event);
	if (event.kind == EventKind::GameCredit)
	{
		++m_snapshot.total_credit_events;
		m_snapshot.credit_events.push_back(event);
		if (m_snapshot.credit_events.size() > MAX_CREDIT_MESSAGES)
			m_snapshot.credit_events.pop_front();
	}
	else
		++m_snapshot.total_events;
	++m_snapshot.revision;
	if (m_snapshot.events.size() > MAX_EVENTS)
		m_snapshot.events.pop_front();
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
		if (attacker >= 4 || victim >= 4 || attacker == victim || record[1] > 1 || record[3] > 1 ||
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
