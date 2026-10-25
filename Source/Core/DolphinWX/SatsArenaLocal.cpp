// Copyright 2026 Sats Arena contributors. Licensed under GPLv2+.
#include "DolphinWX/SatsArenaLocal.h"

#include <algorithm>
#include <cstdlib>
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
static_assert(sizeof(ArenaLocalView) == 9392, "Arena local ABI layout mismatch");

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
		m_rules = new wxStaticText(this, wxID_ANY,
			"Connecting wallet...");
		m_rules->Wrap(310);
		root->Add(m_rules, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 16);
		for (int i = 0; i < 2; ++i)
		{
			auto* row = new wxBoxSizer(wxHORIZONTAL);
			m_qr[i] = new wxStaticBitmap(this, wxID_ANY, InvoiceQR(""));
			m_qr[i]->SetName(wxString::Format("Player %d payment QR code", i + 1));
			row->Add(m_qr[i], 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 12);
			auto* info = new wxBoxSizer(wxVERTICAL);
			auto* player = new wxStaticText(this, wxID_ANY, wxString::Format("PLAYER %d", i + 1));
			info->Add(player, 0, wxBOTTOM, 8);
			m_address[i] = new wxTextCtrl(this, wxID_ANY);
			m_address[i]->SetHint("name@wallet.com");
			m_address[i]->SetMaxLength(127);
			m_address[i]->SetName(wxString::Format("Player %d Lightning Address", i + 1));
			m_address[i]->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { m_addresses_dirty = true; });
			info->Add(m_address[i], 0, wxBOTTOM | wxEXPAND, 8);
			m_verification[i] = new wxStaticText(this, wxID_ANY, "Not verified");
			m_verification[i]->SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
			info->Add(m_verification[i], 0, wxBOTTOM | wxEXPAND, 6);
			m_payment[i] = new wxStaticText(this, wxID_ANY, "Waiting...");
			m_payment[i]->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
			info->Add(m_payment[i], 0, wxBOTTOM, 8);
			m_pay[i] = new wxButton(this, wxID_ANY, "Simulate payment");
			m_pay[i]->SetName(wxString::Format("Simulate Player %d payment", i + 1));
			m_pay[i]->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) { Send(2 + i); });
			info->Add(m_pay[i], 0);
			row->Add(info, 1, wxEXPAND);
			root->Add(row, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 16);

		}
		m_save_addresses = new wxButton(this, wxID_ANY, "Verify receiving addresses");
		m_save_addresses->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			if (!m_handle) return;
			m_addresses_pending = arena_local_addresses(m_handle,
				m_address[0]->GetValue().utf8_str().data(), m_address[1]->GetValue().utf8_str().data());
		});
		root->Add(m_save_addresses, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 16);
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

	void Poll()
	{
		if (!m_enabled)
		{
			m_phase->SetLabel("Payment gate disabled");
			for (int i = 0; i < 2; ++i) { m_pay[i]->Disable(); m_qr[i]->Hide(); }
			return;
		}
		ArenaLocalView view = {};
		if (!m_handle || !arena_local_snapshot(m_handle, &view))
			return;
		if (view.abi_version != 4)
		{
			m_error->SetLabel("Payment engine ABI mismatch");
			return;
		}
		if (!m_addresses_loaded && view.phase != 0)
		{
			for (int i = 0; i < 2; ++i) m_address[i]->ChangeValue(wxString::FromUTF8(view.addresses[i]));
			m_addresses_loaded = true;
		}
		if (m_addresses_pending && !view.busy)
		{
			m_addresses_pending = false;
			if (!view.error[0])
			{
				m_addresses_dirty = false;
				for (int i = 0; i < 2; ++i) m_address[i]->ChangeValue(wxString::FromUTF8(view.addresses[i]));
			}
		}
		const bool addresses_saved = view.addresses[0][0] && view.addresses[1][0] &&
			!m_addresses_dirty && !m_addresses_pending &&
			view.verification_state[0] == 2 && view.verification_state[1] == 2;
		const bool addresses_editable = view.phase == 1 && !view.paid_sats[0] && !view.paid_sats[1];
		for (auto* address : m_address) address->Enable(addresses_editable);
		const bool can_verify = view.phase == 1 || view.phase == 2;
		m_save_addresses->Enable(can_verify && !view.busy && !m_addresses_pending && !addresses_saved);
		m_save_addresses->Show(can_verify);
		m_verification_note->Show(can_verify);
		auto& gate = SatsArena::GetPaymentGate();
		const auto css = gate.Read();
		const auto match = SatsArena::GetTracker().Read();
		// Match events are observed on the core thread; wallet actions stay asynchronous.
		if (m_start_pending && !view.busy)
		{
			m_start_pending = false;
			if (view.phase == 3 && !view.error[0])
			{
				if (gate.Authorize(m_generation))
					m_admitted = true;
				else
					m_abandon = true; // Selection changed while the worker admitted.
			}
		}
		if (m_admitted && match.match_number != m_match_number &&
			(match.status == SatsArena::TrackingState::LocalMatch ||
			 match.status == SatsArena::TrackingState::Finished))
			m_in_match = true;
		if (m_credit_pending && !view.busy)
		{
			m_credit_pending = false;
			if (!view.error[0]) m_last_credits = m_pending_credits;
		}
		if (m_in_match && view.phase == 3 && !view.busy && !match.credit_stream_incomplete)
		{
			const std::array<std::uint64_t, 2> totals = {{match.damage_centi[0], match.damage_centi[1]}};
			if (totals != m_last_credits)
			{
				if (arena_local_credit(m_handle, totals[0], totals[1]))
				{
					m_pending_credits = totals;
					m_credit_pending = true;
				}
				return; // Flush the latest live payouts before submitting the game result.
			}
		}
		if (!view.busy && view.phase == 3)
		{
			if (m_in_match && match.status == SatsArena::TrackingState::Finished)
			{
				const unsigned result = match.game_ended && !match.credit_stream_incomplete && match.winner_port >= 0 &&
					match.winner_port <= 1 ? 6 + match.winner_port : 8;
				if (Send(result)) { m_in_match = false; m_admitted = false; m_settling = true; }
			}
			else if (!m_settling && (m_abandon || (!m_admitted && !m_start_pending)))
			{
				// Recover a previous interrupted fake session without inventing a winner.
				if (Send(8)) { m_abandon = false; m_admitted = false; }
			}
			else if (m_admitted && !m_in_match &&
					 (!Core::IsRunning() || css.generation != m_generation))
			{
				gate.Reset();
				if (Send(8)) m_admitted = false;
			}
		}
		else if (!view.busy && view.phase == 4 && css.ready)
		{
			m_settling = false;
			Send(1); // Two fresh invoices at the next PRESS START prompt.
		}
		else if (!view.busy && view.phase == 2 && css.ready && css.requested &&
				 !m_start_pending && !view.error[0])
		{
			m_generation = css.generation;
			m_match_number = match.match_number;
			m_last_credits = {};
			m_credit_pending = false;
			m_start_pending = Send(4); // Single-use admission must be acknowledged before grant.
		}

		wxString phase;
		if (view.phase == 0) phase = "Starting payment engine...";
		else if (view.phase == 7 || view.error[0]) phase = "Payment needs attention";
		else if (m_addresses_pending) phase = "Verifying receiving addresses...";
		else if (!addresses_saved && (view.phase == 1 || view.phase == 2)) phase = "Verify both Lightning Addresses";
		else if (view.phase == 6) phase = "Sending remaining match payouts...";
		else if (m_in_match) phase = "Match in progress";
		else if (m_admitted) phase = "Both paid • choose a stage";
		else if (!css.ready && match.game_ended)
			phase = match.winner_port >= 0 ? wxString::Format("Player %d wins • next match", match.winner_port + 1) : wxString("No contest • next match");
		else if (!css.ready) phase = "Choose two characters (P1 / P2)";
		else if (view.phase == 2) phase = css.requested ? "Confirming match admission..." : "Both paid • PRESS START";
		else if (view.phase == 3 || view.phase == 4) phase = "Settling previous match...";
		else phase = css.requested ? "Start queued • waiting for payment" : "PRESS START • waiting for payment";
		m_phase->SetLabel(phase);
		m_phase->Wrap(310);
		m_rules->SetLabel(wxString::Format(
			"%s\n%llu sats each match • %llu sats / 1%%\n5-second batches • winner gets the rest",
			view.simulated ? "FAKE WALLET • simulated payments" : "LIGHTNING WALLET • live payments",
			static_cast<unsigned long long>(view.entry_sats),
			static_cast<unsigned long long>(view.sats_per_percent)));
		m_rules->Wrap(310);
		for (int i = 0; i < 2; ++i)
		{
			const unsigned status = m_addresses_dirty ? 0 : view.verification_state[i];
			wxString verification;
			wxColour colour(150, 160, 172);
			if (m_addresses_pending) verification = "Checking receiver...";
			else if (status == 2)
			{
				verification = wxString::Format("Verified • %llu–%llu sats",
					static_cast<unsigned long long>((view.min_sendable_msat[i] + 999) / 1000),
					static_cast<unsigned long long>(view.max_sendable_msat[i] / 1000));
				colour = wxColour(160, 230, 150);
			}
			else if (status == 3 || status == 5)
			{
				verification = wxString::FromUTF8(view.verification_message[i]);
				colour = wxColour(255, 160, 135);
			}
			else if (status == 4) verification = "Verification expired • verify again";
			else verification = "Not verified";
			m_verification[i]->SetLabel(verification);
			m_verification[i]->Wrap(165);
			m_verification[i]->SetForegroundColour(colour);
			m_verification[i]->SetToolTip(wxString::FromUTF8(view.verification_message[i]));
			m_payment[i]->SetLabel(wxString::Format(
				"Entry: %s\nFunds left: %llu sats\nPaid out: %llu sats\nPending: %llu sats (%llus)",
				view.paid_sats[i] ? "PAID" : "Awaiting payment",
				static_cast<unsigned long long>(view.remaining_sats[i]),
				static_cast<unsigned long long>(view.earned_sats[i]),
				static_cast<unsigned long long>(view.pending_sats[i]),
				static_cast<unsigned long long>((view.batch_remaining_ms[i] + 999) / 1000)));
			const bool show = css.ready && addresses_saved && !m_admitted && (view.phase == 1 || view.phase == 2);
			const wxString invoice = show && view.invoices[i][0] ? wxString::FromUTF8(view.invoices[i]) : "";
			if (invoice != m_last_invoice[i])
			{
				m_qr[i]->SetBitmap(InvoiceQR(invoice.utf8_str().data()));
				m_last_invoice[i] = invoice;
			}
			m_qr[i]->Show(!invoice.empty()); // Keep both current-match QR codes after payment.

			m_pay[i]->SetLabel(view.paid_sats[i] ? "Paid" : "Simulate payment");
			m_pay[i]->Show(view.simulated && (view.phase == 1 || view.phase == 2));
			m_pay[i]->Enable(show && view.phase == 1 && !view.busy && view.paid_sats[i] == 0);
		}
		m_error->SetLabel(wxString::FromUTF8(view.error));
		m_error->Wrap(310);
		wxString activity;
		if (match.credit_stream_incomplete) activity += "Credit capture incomplete • remaining funds will be returned\n";
		// Keep damage context compact; payment receipts are the primary log.
		const auto first = match.credit_events.size() > 5 ? match.credit_events.size() - 5 : 0;
		for (std::size_t i = first; i < match.credit_events.size(); ++i)
		{
			const auto& event = match.credit_events[i];
			activity += wxString::Format("f%d P%d -> P%d +%.1f damage\n", event.frame, event.attacker + 1, event.victim + 1, event.amount);
		}
		if (match.game_ended)
			activity += match.winner_port >= 0 ? wxString::Format("Player %d wins\n", match.winner_port + 1) : wxString("No contest\n");
		activity += "\n";
		activity += wxString::FromUTF8(view.payment_log);
		if (m_log->GetValue() != activity)
		{
			m_log->ChangeValue(activity);
			m_log->ShowPosition(m_log->GetLastPosition());
		}
		Layout();
		FitInside();
	}

	ArenaLocalHandle* m_handle = nullptr;
	wxTimer m_timer;
	bool m_enabled = true, m_start_pending = false, m_admitted = false;
	bool m_in_match = false, m_abandon = false, m_settling = false;
	std::uint64_t m_generation = 0, m_match_number = 0, m_log_revision = ~std::uint64_t(0);
	std::array<std::uint64_t, 2> m_last_credits = {}, m_pending_credits = {};
	bool m_credit_pending = false;
	wxStaticText* m_rules = nullptr;
	wxStaticText* m_phase = nullptr;
	wxStaticText* m_error = nullptr;
	wxStaticText* m_payment[2] = {};
	wxStaticText* m_verification[2] = {};
	wxStaticText* m_verification_note = nullptr;
	wxStaticBitmap* m_qr[2] = {};
	wxTextCtrl* m_address[2] = {};
	wxButton* m_save_addresses = nullptr;
	bool m_addresses_loaded = false, m_addresses_dirty = false, m_addresses_pending = false;
	wxTextCtrl* m_log = nullptr;
	wxButton* m_pay[2] = {};
	wxString m_last_invoice[2];
};
}
wxWindow* CreateSidebar(wxWindow* parent) { return new FundingPanel(parent); }
}
