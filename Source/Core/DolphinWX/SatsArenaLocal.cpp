// Copyright 2026 Sats Arena contributors. Licensed under GPLv2+.
#include "DolphinWX/SatsArenaLocal.h"

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
static_assert(sizeof(ArenaLocalView) == 616, "Arena local ABI layout mismatch");

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
	constexpr int size = 116;
	wxBitmap bitmap(size, size);
	wxMemoryDC dc(bitmap);
	dc.SetBackground(*wxWHITE_BRUSH);
	dc.Clear();
	if (*invoice)
	{
		const auto qr = qrcodegen::QrCode::encodeText(invoice, qrcodegen::QrCode::Ecc::MEDIUM);
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
			"FAKE WALLET • simulated payments");
		m_rules->Wrap(310);
		root->Add(m_rules, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 16);
		for (int i = 0; i < 2; ++i)
		{
			auto* row = new wxBoxSizer(wxHORIZONTAL);
			m_qr[i] = new wxStaticBitmap(this, wxID_ANY, InvoiceQR(""));
			m_qr[i]->SetName(wxString::Format("Player %d payment QR code", i + 1));
			row->Add(m_qr[i], 0, wxRIGHT, 12);
			auto* info = new wxBoxSizer(wxVERTICAL);
			auto* player = new wxStaticText(this, wxID_ANY, wxString::Format("PLAYER %d", i + 1));
			info->Add(player, 0, wxBOTTOM, 8);
			m_payment[i] = new wxStaticText(this, wxID_ANY, "Waiting...");
			info->Add(m_payment[i], 0, wxBOTTOM, 8);
			m_pay[i] = new wxButton(this, wxID_ANY, "Simulate payment");
			m_pay[i]->SetName(wxString::Format("Simulate Player %d payment", i + 1));
			m_pay[i]->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) { Send(2 + i); });
			info->Add(m_pay[i], 0);
			row->Add(info, 1, wxEXPAND);
			root->Add(row, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 16);
			m_invoice[i] = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
			m_invoice[i]->SetName(wxString::Format("Player %d invoice", i + 1));
			root->Add(m_invoice[i], 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 16);
		}
		m_error = new wxStaticText(this, wxID_ANY, "");
		m_error->SetForegroundColour(wxColour(255, 160, 135));
		root->Add(m_error, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 16);
		root->Add(new wxStaticText(this, wxID_ANY, "Match activity"), 0, wxLEFT | wxRIGHT | wxBOTTOM, 16);
		m_log = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(-1, 130),
			wxTE_MULTILINE | wxTE_READONLY);
		m_log->SetBackgroundColour(wxColour(20, 25, 31));
		m_log->SetForegroundColour(wxColour(244, 246, 248));
		m_log->SetFont(wxFont(9, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
		root->Add(m_log, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 16);
		auto* help = new wxStaticText(this, wxID_ANY,
			"Two fresh entry payments per match.\nEarn sats on hits; the winner gets the remaining pot.");
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
		if (view.abi_version != 2)
		{
			m_error->SetLabel("Payment engine ABI mismatch");
			return;
		}
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
			"FAKE WALLET • simulated payments\n%llu sats each match • %llu sats / 1%%\nWinner gets the remaining pot",
			static_cast<unsigned long long>(view.entry_sats),
			static_cast<unsigned long long>(view.sats_per_percent)));
		m_rules->Wrap(310);
		for (int i = 0; i < 2; ++i)
		{
			m_payment[i]->SetLabel(wxString::Format(
				"Entry: %s\nMatch funds: %llu sats\nEarned: %llu sats\nSending: %llu sats",
				view.paid_sats[i] ? "PAID" : "Awaiting payment",
				static_cast<unsigned long long>(view.remaining_sats[i]),
				static_cast<unsigned long long>(view.earned_sats[i]),
				static_cast<unsigned long long>(view.pending_sats[i])));
			const bool show = css.ready && !m_admitted && (view.phase == 1 || view.phase == 2);
			const wxString invoice = show && view.invoices[i][0] ? wxString::FromUTF8(view.invoices[i]) : "";
			if (invoice != m_last_invoice[i])
			{
				m_qr[i]->SetBitmap(InvoiceQR(invoice.utf8_str().data()));
				m_last_invoice[i] = invoice;
			}
			m_qr[i]->Show(!invoice.empty()); // Keep both current-match QR codes after payment.
			const wxString invoice_label = !invoice.empty() ? invoice :
				wxString(view.phase == 3 ? "Match in progress" : "Two new invoices at PRESS START");
			if (m_invoice[i]->GetValue() != invoice_label)
				m_invoice[i]->ChangeValue(invoice_label);
			m_pay[i]->SetLabel(view.paid_sats[i] ? "Paid" : "Simulate payment");
			m_pay[i]->Enable(show && view.phase == 1 && !view.busy && view.paid_sats[i] == 0);
		}
		m_error->SetLabel(wxString::FromUTF8(view.error));
		m_error->Wrap(310);
		if (m_log_revision != match.revision)
		{
			m_log_revision = match.revision;
			wxString text;
			if (match.credit_stream_incomplete) text += "Credit capture incomplete • remaining funds will be returned\n";
			for (const auto& event : match.credit_events)
				text += wxString::Format("f%d  P%d -> P%d +%.1f\n", event.frame,
					event.attacker + 1, event.victim + 1, event.amount);
			if (match.game_ended)
				text += match.winner_port >= 0 ? wxString::Format("Player %d wins\n", match.winner_port + 1) : "No contest / tied result\n";
			m_log->ChangeValue(text);
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
	wxStaticBitmap* m_qr[2] = {};
	wxTextCtrl* m_invoice[2] = {};
	wxTextCtrl* m_log = nullptr;
	wxButton* m_pay[2] = {};
	wxString m_last_invoice[2];
};
}
wxWindow* CreateSidebar(wxWindow* parent) { return new FundingPanel(parent); }
}
