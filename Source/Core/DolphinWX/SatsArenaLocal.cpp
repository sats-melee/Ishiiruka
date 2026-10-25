// Copyright 2026 Sats Arena contributors. Licensed under GPLv2+.
#include "DolphinWX/SatsArenaLocal.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <wx/panel.h>
#include <cstring>
#include <wx/button.h>
#include <wx/dcmemory.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>

#include "Common/FileUtil.h"
#include "Core/Core.h"
#include "Core/Slippi/SatsArenaTracker.h"
#include "DolphinWX/QR/qrcodegen.hpp"
#include "sats_arena_local.h"
static_assert(sizeof(ArenaLocalView) == 14456, "Arena local ABI layout mismatch");

namespace SatsArenaLocal
{
namespace
{
struct Host
{
	ArenaLocalHandle* handle = nullptr;
	~Host() { arena_local_destroy(handle); }
	ArenaLocalHandle* Get()
	{
		if (!handle)
		{
			const char* override_path = std::getenv("SATS_ARENA_FAKE_DATA_DIR");
			const std::string folder = override_path && *override_path ? override_path :
				File::GetUserPath(D_USER_IDX) + "SatsArenaFake";
			handle = arena_local_create(folder.c_str());
		}
		return handle;
	}
};
Host& GetHost() { static Host host; return host; }

wxBitmap InvoiceQR(const char* invoice)
{
	const auto qr = qrcodegen::QrCode::encodeText(invoice, qrcodegen::QrCode::Ecc::MEDIUM);
	const int size = std::max(116, qr.getSize() + 8);
	wxBitmap bitmap(size, size);
	wxMemoryDC dc(bitmap);
	dc.SetBackground(*wxWHITE_BRUSH);
	dc.Clear();
	if (*invoice)
	{
		const int modules = qr.getSize();
		const int scale = size / (modules + 8);
		const int offset = (size - modules * scale) / 2;
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(*wxBLACK_BRUSH);
		for (int y = 0; y < modules; ++y)
			for (int x = 0; x < modules; ++x)
				if (qr.getModule(x, y))
					dc.DrawRectangle(offset + x * scale, offset + y * scale, scale, scale);
	}
	dc.SelectObject(wxNullBitmap);
	return bitmap;
}

class FundingPanel final : public wxScrolledWindow
{
public:
	explicit FundingPanel(wxWindow* parent)
		: wxScrolledWindow(parent, wxID_ANY), m_timer(this)
	{
		SetBackgroundColour(wxColour(16, 20, 25));
		SetForegroundColour(wxColour(244, 246, 248));
		SetScrollRate(0, 12);
		SetName("Sats Arena match funding");
		const char* env = std::getenv("SATS_ARENA_FAKE_LOCAL");
		m_enabled = !env || std::strcmp(env, "0") != 0;
		SatsArena::GetPaymentGate().Enable(m_enabled);
		m_handle = m_enabled ? GetHost().Get() : nullptr;
		auto* root = new wxBoxSizer(wxVERTICAL);
		auto* title = new wxStaticText(this, wxID_ANY, "SATS ARENA");
		title->SetFont(wxFont(17, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
		root->Add(title, 0, wxALL, 16);
		m_phase = new wxStaticText(this, wxID_ANY, "Starting payment engine...");
		m_phase->SetFont(wxFont(11, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
		root->Add(m_phase, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 16);
		m_address_warning = new wxStaticText(this, wxID_ANY, "Match blocked: verify both players' receiving addresses.");
		m_address_warning->SetForegroundColour(wxColour(255, 110, 110));
		m_address_warning->Wrap(310);
		root->Add(m_address_warning, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 16);
		m_roster = new wxStaticText(this, wxID_ANY, "Waiting for character selection...");
		m_roster->SetName("Live match roster and teams");
		root->Add(m_roster, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 16);
		m_rules = new wxStaticText(this, wxID_ANY,
			"Connecting wallet...");
		m_rules->Wrap(310);
		root->Add(m_rules, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 16);
		for (int i = 0; i < 4; ++i)
		{
			auto* card = new wxPanel(this);
			m_rows[i] = card;
			card->SetBackgroundColour(wxColour(16, 20, 25));
			card->SetForegroundColour(wxColour(244, 246, 248));
			auto* info = new wxBoxSizer(wxVERTICAL);
			m_player[i] = new wxStaticText(card, wxID_ANY, wxString::Format("PLAYER %d", i + 1));
			info->Add(m_player[i], 0, wxBOTTOM, 6);
			m_address[i] = new wxTextCtrl(card, wxID_ANY);
			m_address[i]->SetHint("name@wallet.com");
			m_address[i]->SetMaxLength(127);
			m_address[i]->SetMinSize(wxSize(80, -1));
			m_address[i]->SetName(wxString::Format("Player %d Lightning Address", i + 1));
			m_address[i]->Bind(wxEVT_TEXT, [this, i](wxCommandEvent&) { m_addresses_dirty[i] = true; });
			auto* address_row = new wxBoxSizer(wxHORIZONTAL);
			address_row->Add(m_address[i], 1, wxRIGHT | wxALIGN_CENTER_VERTICAL, 6);
			m_verify[i] = new wxButton(card, wxID_ANY, "Verify", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
			m_verify[i]->SetName(wxString::Format("Verify Player %d receiving address", i + 1));
			m_verify[i]->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) {
				if (m_handle && arena_local_verify_address(m_handle, i, m_address[i]->GetValue().utf8_str().data()))
					m_address_pending = i;
			});
			address_row->Add(m_verify[i], 0);
			info->Add(address_row, 0, wxBOTTOM | wxEXPAND, 6);
			m_verification[i] = new wxStaticText(card, wxID_ANY, "Not verified");
			m_verification[i]->SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
			info->Add(m_verification[i], 0, wxBOTTOM | wxEXPAND, 6);
			auto* body = new wxBoxSizer(wxHORIZONTAL);
			m_qr[i] = new wxStaticBitmap(card, wxID_ANY, InvoiceQR(""));
			m_qr[i]->SetName(wxString::Format("Player %d payment QR code", i + 1));
			body->Add(m_qr[i], 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 12);
			auto* payment = new wxBoxSizer(wxVERTICAL);
			m_payment[i] = new wxStaticText(card, wxID_ANY, "Waiting...");
			m_payment[i]->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
			payment->Add(m_payment[i], 0, wxBOTTOM, 6);
			m_pay[i] = new wxButton(card, wxID_ANY, "Simulate payment");
			m_pay[i]->SetName(wxString::Format("Simulate Player %d payment", i + 1));
			m_pay[i]->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) { Send(i < 2 ? 2 + i : 7 + i); });
			payment->Add(m_pay[i], 0);
			body->Add(payment, 1, wxEXPAND);
			info->Add(body, 0, wxEXPAND);
			card->SetSizer(info);
			root->Add(card, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 16);
		}
		m_verification_note = new wxStaticText(this, wxID_ANY, "Checks the receiver and an unpaid invoice. No funds are sent.");
		m_verification_note->Wrap(310);
		root->Add(m_verification_note, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 16);
		m_error = new wxStaticText(this, wxID_ANY, "");
		m_error->SetForegroundColour(wxColour(255, 160, 135));
		root->Add(m_error, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 16);
		root->Add(new wxStaticText(this, wxID_ANY, "Payments / match activity"), 0, wxLEFT | wxRIGHT | wxBOTTOM, 16);
		m_log = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(-1, 150),
			wxTE_MULTILINE | wxTE_READONLY);
		m_log->SetBackgroundColour(wxColour(20, 25, 31));
		m_log->SetForegroundColour(wxColour(244, 246, 248));
		m_log->SetFont(wxFont(9, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
		root->Add(m_log, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 16);
		auto* help = new wxStaticText(this, wxID_ANY,
			"Hits collect into 5-second payout batches.\nMatch end pays the rest immediately.");
		help->Wrap(310);
		root->Add(help, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 16);
		SetSizer(root);
		Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Poll(); });
		m_timer.Start(100);
		Poll();
	}
	~FundingPanel() override { m_timer.Stop(); }

private:
	bool Send(unsigned action)
	{
		if (!m_handle || !arena_local_action(m_handle, action))
			return false;
		return true;
	}

	static const char* TeamName(unsigned team)
	{
		return team < 3 ? std::array<const char*, 3>{{"Red", "Blue", "Green"}}[team] : "Unknown";
	}
	static bool SameRoster(const ArenaLocalView& view, const SatsArena::GateView& css)
	{
		if (!view.roster_configured || bool(view.teams) != css.teams) return false;
		for (int i = 0; i < 4; ++i)
			if (view.kinds[i] != css.kinds[i] || view.team_ids[i] != css.team_ids[i] ||
				view.characters[i] != css.characters[i]) return false;
		return true;
	}
	void Poll()
	{
		if (!m_enabled) { m_phase->SetLabel("Payment gate disabled"); return; }
		ArenaLocalView view = {};
		if (!m_handle || !arena_local_snapshot(m_handle, &view)) return;
		if (view.abi_version != 5) { m_error->SetLabel("Payment engine ABI mismatch"); return; }
		if (!m_addresses_loaded && view.phase != 0)
		{
			for (int i = 0; i < 4; ++i) m_address[i]->ChangeValue(wxString::FromUTF8(view.addresses[i]));
			m_addresses_loaded = true;
		}
		if (m_address_pending >= 0 && !view.busy)
		{
			const int i = m_address_pending;
			m_address_pending = -1;
			if (!view.error[0])
			{
				m_addresses_dirty[i] = false;
				m_address[i]->ChangeValue(wxString::FromUTF8(view.addresses[i]));
			}
		}
		auto& gate = SatsArena::GetPaymentGate();
		const auto css = gate.Read();
		const auto match = SatsArena::GetTracker().Read();
		const bool roster_matches = SameRoster(view, css);
		bool addresses_saved = m_address_pending < 0;
		bool any_paid = false;
		for (int i = 0; i < 4; ++i)
		{
			if (view.kinds[i] == 0 && (m_addresses_dirty[i] || view.verification_state[i] != 2)) addresses_saved = false;
			any_paid = any_paid || view.paid_sats[i] != 0;
		}
		if (m_credit_pending && !view.busy)
		{
			m_credit_pending = false;
			if (view.error[0]) m_capture_failed = true;
		}
		if (m_start_pending && !view.busy)
		{
			m_start_pending = false;
			if (view.phase == 3 && !view.error[0])
			{
				if (gate.Authorize(m_generation)) m_admitted = true;
				else m_abandon = true;
			}
		}
		if (m_admitted && match.match_number != m_match_number &&
			(match.status == SatsArena::TrackingState::LocalMatch || match.status == SatsArena::TrackingState::Finished ||
			 match.status == SatsArena::TrackingState::Unsupported))
		{
			m_in_match = true;
			if (match.status == SatsArena::TrackingState::Unsupported || bool(view.teams) != match.teams) m_capture_failed = true;
			for (int i = 0; i < 4; ++i)
				if (view.kinds[i] != match.kinds[i] || view.team_ids[i] != match.team_ids[i]) m_capture_failed = true;
		}
		if (m_in_match && view.phase == 3 && !view.busy && !m_capture_failed && !match.credit_stream_incomplete)
		{
			std::vector<ArenaLocalCredit> credits;
			for (const auto& event : match.credit_events)
				if (event.sequence > view.last_credit_sequence)
					credits.push_back({event.sequence, static_cast<std::uint64_t>(std::llround(event.amount * 100.0)),
						static_cast<std::uint32_t>(event.attacker), event.victim});
			if (match.total_credit_events > view.last_credit_sequence &&
				(credits.empty() || credits.front().sequence != view.last_credit_sequence + 1)) m_capture_failed = true;
			else if (!credits.empty())
			{
				m_credit_pending = arena_local_credits(m_handle, credits.data(), credits.size());
				return; // Acknowledge all final credits before submitting the result.
			}
		}
		if (!view.busy && view.phase == 3)
		{
			if (m_in_match && (match.status == SatsArena::TrackingState::Finished || !Core::IsRunning()))
			{
				const int winner = match.game_ended && match.credit_stream_seen && !m_capture_failed && !match.credit_stream_incomplete ? match.winner_port : -1;
				const unsigned result = winner >= 0 && winner < 4 ? (winner < 2 ? 6 + winner : 9 + winner) : 8;
				if (Send(result)) { m_in_match = false; m_admitted = false; m_settling = true; }
			}
			else if (m_abandon || (!m_admitted && !m_start_pending) ||
				(m_admitted && !m_in_match && (!Core::IsRunning() || css.generation != m_generation)))
			{
				if (Send(8)) { m_abandon = false; m_admitted = false; m_settling = true; }
			}
		}
		else if (!view.busy && !m_admitted && !m_start_pending)
		{
			if (view.phase == 4 && css.roster_valid)
			{
				m_settling = false;
				m_capture_failed = false;
				Send(1);
			}
			else if ((view.phase == 1 || view.phase == 2) && css.roster_valid && !roster_matches)
			{
				std::array<std::uint32_t, 4> kinds, teams, characters;
				for (int i = 0; i < 4; ++i) { kinds[i] = css.kinds[i]; teams[i] = css.team_ids[i]; characters[i] = css.characters[i]; }
				arena_local_roster(m_handle, css.teams, kinds.data(), teams.data(), characters.data());
				m_lobby_generation = css.generation;
			}
			else if ((view.phase == 1 || view.phase == 2) && any_paid &&
				(!css.roster_valid || (m_lobby_generation && m_lobby_generation != css.generation)))
			{
				Send(8); // Return funded entries before an altered lobby can start.
			}
			else if (view.phase == 2 && css.ready && css.requested && roster_matches && addresses_saved && !view.error[0])
			{
				m_generation = css.generation;
				m_match_number = match.match_number;
				m_capture_failed = false;
				m_credit_pending = false;
				m_start_pending = Send(4);
			}
			else if (roster_matches) m_lobby_generation = css.generation;
		}
		const bool funding = view.phase == 1 || view.phase == 2;
		const bool show_css = !m_admitted && !m_in_match && (css.kinds[0] < 2 || css.kinds[1] < 2 || css.kinds[2] < 2 || css.kinds[3] < 2);
		wxString roster = (show_css ? css.teams : bool(view.teams)) ? "TEAM BATTLE" : "FREE-FOR-ALL";
		m_roster->SetLabel(roster);
		wxString phase;
		if (view.phase == 0) phase = "Starting payment engine...";
		else if (view.phase == 7 || view.error[0]) phase = "Payment needs attention";
		else if (view.phase == 6) phase = "Sending match payouts...";
		else if (m_in_match) phase = "Match in progress";
		else if (m_admitted) phase = "Entries paid • choose a stage";
		else if (funding && (!css.roster_valid || !roster_matches)) phase = "Choose 2–4 players and opposing sides";
		else if (m_address_pending >= 0) phase = "Verifying receiving address...";
		else if (funding && !addresses_saved) phase = "Verify each human's address";
		else if (view.phase == 2) phase = css.requested ? "Confirming match admission..." : "Entries paid • PRESS START";
		else if (view.phase == 4) phase = "Match settled • next match";
		else phase = css.requested ? "Start queued • waiting for entries" : "PRESS START • waiting for entries";
		m_phase->SetLabel(phase);
		m_phase->Wrap(310);
		wxString warning;
		if (funding && (!css.roster_valid || !roster_matches)) warning = "Match blocked: finish selecting opposing players.";
		else if (funding && !addresses_saved) warning = "Match blocked: verify every human's receiving address.";
		else if (view.phase == 1) warning = "Match blocked: every human must pay their entry.";
		m_address_warning->SetLabel(warning);
		m_address_warning->Wrap(310);
		m_address_warning->Show(!warning.empty());
		m_rules->SetLabel(wxString::Format("%s\n%llu sats per human • %llu sats / 1%%\n5-second batches • winners split opponents' pots",
			view.simulated ? "FAKE WALLET • simulated payments" : "LIGHTNING WALLET • live payments",
			static_cast<unsigned long long>(view.entry_sats), static_cast<unsigned long long>(view.sats_per_percent)));
		m_rules->Wrap(310);
		m_verification_note->Show(funding);
		for (int i = 0; i < 4; ++i)
		{
			const unsigned kind = show_css ? css.kinds[i] : view.kinds[i];
			const unsigned team = show_css ? css.team_ids[i] : view.team_ids[i];
			const bool teams = show_css ? css.teams : bool(view.teams);
			const bool human = kind == 0;
			m_rows[i]->Show(kind < 2);
			wxString label = wxString::Format("PLAYER %d • %s", i + 1, human ? "Human" : "CPU");
			if (teams && team < 3) label += wxString::Format(" • %s Team", TeamName(team));
			m_player[i]->SetLabel(label);
			m_address[i]->Show(human);
			m_verify[i]->Show(human && funding);
			m_address[i]->Enable(human && funding && !any_paid && !view.busy);
			m_verify[i]->Enable(funding && roster_matches && !view.busy && m_address_pending < 0);
			const unsigned status = m_addresses_dirty[i] ? 0 : view.verification_state[i];
			wxString verification;
			wxColour colour(150, 160, 172);
			if (!human) verification = "No address or entry required";
			else if (m_address_pending == i) verification = "Checking receiver...";
			else if (status == 2)
			{
				verification = wxString::Format("Verified • %llu–%llu sats",
					static_cast<unsigned long long>((view.min_sendable_msat[i] + 999) / 1000),
					static_cast<unsigned long long>(view.max_sendable_msat[i] / 1000));
				colour = wxColour(160, 230, 150);
			}
			else if (status == 3 || status == 5) { verification = wxString::FromUTF8(view.verification_message[i]); colour = wxColour(255, 160, 135); }
			else if (status == 4) verification = "Verification expired • verify again";
			else verification = "Not verified";
			m_verification[i]->SetLabel(verification);
			m_verification[i]->Wrap(290);
			m_verification[i]->SetForegroundColour(colour);
			m_verification[i]->SetToolTip(wxString::FromUTF8(view.verification_message[i]));
			m_payment[i]->SetLabel(human ? wxString::Format("Entry: %s\nPot: %llu sats\nPaid out: %llu sats\nPending: %llu sats (%llus)",
				view.paid_sats[i] ? "PAID" : "Awaiting payment", static_cast<unsigned long long>(view.remaining_sats[i]),
				static_cast<unsigned long long>(view.earned_sats[i]), static_cast<unsigned long long>(view.pending_sats[i]),
				static_cast<unsigned long long>((view.batch_remaining_ms[i] + 999) / 1000)) : wxString("CPU • no payments"));
			const bool show = human && css.ready && roster_matches && addresses_saved && !m_admitted && funding;
			const wxString invoice = show && view.invoices[i][0] ? wxString::FromUTF8(view.invoices[i]) : "";
			if (invoice != m_last_invoice[i]) { m_qr[i]->SetBitmap(InvoiceQR(invoice.utf8_str().data())); m_last_invoice[i] = invoice; }
			m_qr[i]->Show(!invoice.empty());
			m_pay[i]->SetLabel(view.paid_sats[i] ? "Paid" : "Simulate payment");
			m_pay[i]->Show(human && view.simulated && funding);
			m_pay[i]->Enable(show && view.phase == 1 && !view.busy && view.paid_sats[i] == 0);
			m_rows[i]->Layout();
		}
		m_error->SetLabel(wxString::FromUTF8(view.error));
		m_error->Wrap(310);
		wxString activity;
		if ((m_admitted || m_in_match) && (m_capture_failed || match.credit_stream_incomplete)) activity = "Incomplete match data • unspent funds will be returned\n";
		activity += wxString::FromUTF8(view.payment_log);
		if (m_log->GetValue() != activity) { m_log->ChangeValue(activity); m_log->ShowPosition(m_log->GetLastPosition()); }
		Layout();
		FitInside();
	}

	ArenaLocalHandle* m_handle = nullptr;
	wxTimer m_timer;
	bool m_enabled = true, m_start_pending = false, m_admitted = false;
	bool m_in_match = false, m_abandon = false, m_settling = false;
	std::uint64_t m_generation = 0, m_match_number = 0, m_log_revision = ~std::uint64_t(0);
	std::uint64_t m_lobby_generation = 0;
	bool m_capture_failed = false;
	bool m_credit_pending = false;
	wxStaticText* m_rules = nullptr;
	wxStaticText* m_roster = nullptr;
	wxStaticText* m_phase = nullptr;
	wxStaticText* m_error = nullptr;
	wxPanel* m_rows[4] = {};
	wxStaticText* m_player[4] = {};
	wxStaticText* m_payment[4] = {};
	wxStaticText* m_verification[4] = {};
	wxStaticText* m_verification_note = nullptr;
	wxStaticBitmap* m_qr[4] = {};
	wxTextCtrl* m_address[4] = {};
	wxButton* m_verify[4] = {};
	wxStaticText* m_address_warning = nullptr;
	bool m_addresses_loaded = false, m_addresses_dirty[4] = {};
	int m_address_pending = -1;
	wxTextCtrl* m_log = nullptr;
	wxButton* m_pay[4] = {};
	wxString m_last_invoice[4];
};
}
wxWindow* CreateSidebar(wxWindow* parent) { return new FundingPanel(parent); }
}
