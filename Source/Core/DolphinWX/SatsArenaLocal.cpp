// Copyright 2026 Sats Arena contributors. Licensed under GPLv2+.
#include "DolphinWX/SatsArenaLocal.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <chrono>
#include <cmath>
#include <vector>
#include <wx/panel.h>
#include <cstring>
#include <wx/button.h>
#include <wx/checkbox.h>
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
static_assert(sizeof(ArenaLocalView) == 14784, "Arena local ABI layout mismatch");

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
            const char* live_override = std::getenv("SATS_ARENA_LIVE_DATA_DIR");
            const std::string live_folder = live_override && *live_override ? live_override :
                File::GetUserPath(D_USER_IDX) + "SatsArenaLive";
            handle = arena_local_create_configured(folder.c_str(), live_folder.c_str());
		}
		return handle;
	}
};
Host& GetHost() { static Host host; return host; }

const wxColour BACKGROUND(16, 17, 22);
const wxColour SURFACE(25, 27, 34);
const wxColour TEXT(233, 234, 234);
const wxColour MUTED(166, 170, 181);
const wxColour BORDER(48, 51, 61);
const wxColour ACCENT(245, 139, 148);
const wxColour GREEN(143, 209, 166);
const int CONTENT_WIDTH = 304;
const int MARGIN = 14;

wxStaticText* Label(wxWindow* parent, const wxString& text, int points,
                    const wxColour& colour = TEXT, bool bold = false, long style = 0)
{
    auto* label = new wxStaticText(parent, wxID_ANY, text, wxDefaultPosition, wxDefaultSize, style);
    label->SetForegroundColour(colour);
    label->SetFont(wxFont(points, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                         bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL));
    return label;
}
wxPanel* Divider(wxWindow* parent)
{
    auto* line = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    line->SetBackgroundColour(BORDER);
    line->SetMinSize(wxSize(-1, 1));
    return line;
}
wxBitmap InvoiceQR(const char* invoice)
{
    if (!invoice || !*invoice) return wxBitmap(1, 1);
    const auto qr = qrcodegen::QrCode::encodeText(invoice, qrcodegen::QrCode::Ecc::MEDIUM);
    const int modules = qr.getSize();
    const int quiet = 4;
    const int scale = std::max(1, 176 / (modules + quiet * 2));
    // Use an exact module grid: only the required four-module quiet zone,
    // with no leftover white padding or blurry fractional scaling.
    const int size = (modules + quiet * 2) * scale;
    wxBitmap bitmap(size, size);
    wxMemoryDC dc(bitmap);
    dc.SetBackground(*wxWHITE_BRUSH);
    dc.Clear();
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(*wxBLACK_BRUSH);
    for (int y = 0; y < modules; ++y)
        for (int x = 0; x < modules; ++x)
            if (qr.getModule(x, y))
                dc.DrawRectangle((quiet + x) * scale, (quiet + y) * scale, scale, scale);
    dc.SelectObject(wxNullBitmap);
    return bitmap;
}

class FundingPanel final : public wxScrolledWindow
{
public:
    explicit FundingPanel(wxWindow* parent)
        : wxScrolledWindow(parent, wxID_ANY), m_timer(this)
    {
        SetBackgroundColour(BACKGROUND);
        SetForegroundColour(TEXT);
        SetScrollRate(0, 12);
        SetName("Super Sats Bro Melee match funding");
        const char* env = std::getenv("SATS_ARENA_FAKE_LOCAL");
        m_enabled = !env || std::strcmp(env, "0") != 0;
        SatsArena::GetPaymentGate().Enable(m_enabled);
        m_handle = m_enabled ? GetHost().Get() : nullptr;
        auto* root = new wxBoxSizer(wxVERTICAL);
        root->Add(Label(this, "Super Sats Bro Melee", 15, TEXT, true),
                  0, wxLEFT | wxRIGHT | wxTOP, MARGIN);
        m_phase = Label(this, "Connecting wallet...", 10, MUTED);
        root->Add(m_phase, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM | wxEXPAND, MARGIN);
        m_background_payments = Label(this, "", 9, MUTED);
        root->Add(m_background_payments, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, MARGIN);
        m_background_payments->Hide();

        auto* rules = new wxBoxSizer(wxHORIZONTAL);
        auto* entry_rule = new wxBoxSizer(wxVERTICAL);
        entry_rule->Add(Label(this, "ENTRY / PLAYER", 8, MUTED), 0, wxBOTTOM, 4);
        m_entry_rule = Label(this, "...", 15, TEXT, true, wxST_ELLIPSIZE_END);
        m_entry_rule->SetMinSize(wxSize(100, -1));
        entry_rule->Add(m_entry_rule, 0, wxEXPAND);
        rules->Add(entry_rule, 1, wxRIGHT, 10);
        auto* rate_rule = new wxBoxSizer(wxVERTICAL);
        rate_rule->Add(Label(this, "DAMAGE PAYOUT", 8, MUTED), 0, wxBOTTOM, 4);
        m_rate_rule = Label(this, "...", 15, ACCENT, true, wxST_ELLIPSIZE_END);
        m_rate_rule->SetMinSize(wxSize(100, -1));
        rate_rule->Add(m_rate_rule, 0, wxEXPAND);
        rules->Add(rate_rule, 1);
        root->Add(rules, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, MARGIN);
        m_invoice_rule = Label(this, "", 9, MUTED);
        m_invoice_rule->SetToolTip("The routing contribution is added to the invoice and retained by the host wallet for fees, including after cancellation. It is separate from the playable pot.");
        root->Add(m_invoice_rule, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, MARGIN);
        m_wallet_mode = Label(this, "", 8, ACCENT);
        root->Add(m_wallet_mode, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, MARGIN);
        m_wallet_mode->Hide();
        m_address_warning = Label(this, "", 9, ACCENT);
        root->Add(m_address_warning, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, MARGIN);
        m_address_warning->Hide();
        root->Add(Divider(this), 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, MARGIN);

        for (int i = 0; i < 4; ++i)
        {
            auto* row = new wxPanel(this);
            m_rows[i] = row;
            row->SetBackgroundColour(BACKGROUND);
            row->SetForegroundColour(TEXT);
            auto* info = new wxBoxSizer(wxVERTICAL);
            auto* heading = new wxBoxSizer(wxHORIZONTAL);
            m_player[i] = Label(row, wxString::Format("PLAYER %d • Human", i + 1), 10, TEXT, true, wxST_ELLIPSIZE_END);
            m_player[i]->SetMinSize(wxSize(80, -1));
            heading->Add(m_player[i], 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
            auto* entry = new wxBoxSizer(wxVERTICAL);
            entry->Add(Label(row, "ENTRY", 7, MUTED), 0, wxALIGN_RIGHT | wxBOTTOM, 2);
            m_entry_status[i] = Label(row, "UNPAID", 11, ACCENT, true);
            entry->Add(m_entry_status[i], 0, wxALIGN_RIGHT);
            heading->Add(entry, 0);
            info->Add(heading, 0, wxEXPAND | wxBOTTOM, 9);

            m_address[i] = new wxTextCtrl(row, wxID_ANY);
            m_address[i]->SetHint("name@wallet.com");
            m_address[i]->SetMaxLength(127);
            m_address[i]->SetMinSize(wxSize(80, -1));
            m_address[i]->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
            m_address[i]->SetBackgroundColour(wxColour(36, 38, 46));
            m_address[i]->SetForegroundColour(TEXT);
            m_address[i]->SetName(wxString::Format("Player %d Lightning Address", i + 1));
            m_address[i]->Bind(wxEVT_TEXT, [this, i](wxCommandEvent&) {
                m_addresses_dirty[i] = true;
                m_edited_at[i] = std::chrono::steady_clock::now();
                m_auto_failure[i].clear();
                m_auto_attempted[i].clear();
            });
            auto* address_row = new wxBoxSizer(wxHORIZONTAL);
            address_row->Add(m_address[i], 1, wxRIGHT | wxALIGN_CENTER_VERTICAL, 6);
            m_verify[i] = new wxButton(row, wxID_ANY, "Retry", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
            m_verify[i]->SetName(wxString::Format("Retry Player %d receiving address", i + 1));
            m_verify[i]->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) {
                m_auto_attempted[i].clear();
                m_auto_failure[i].clear();
            });
            address_row->Add(m_verify[i], 0);
            info->Add(address_row, 0, wxBOTTOM | wxEXPAND, 4);
            m_verification[i] = Label(row, "Not verified", 8, MUTED);
            info->Add(m_verification[i], 0, wxBOTTOM | wxEXPAND, 9);

            m_body[i] = new wxBoxSizer(wxHORIZONTAL);
            m_qr[i] = new wxStaticBitmap(row, wxID_ANY, InvoiceQR(""));
            m_qr[i]->SetName(wxString::Format("Player %d payment QR code", i + 1));
            m_body[i]->Add(m_qr[i], 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 10);
            m_qr[i]->Hide();
            m_metrics[i] = new wxGridSizer(1, 3, 8, 8);
            auto metric = [&](const char* caption, const wxColour& colour) {
                auto* column = new wxBoxSizer(wxVERTICAL);
                column->Add(Label(row, caption, 8, MUTED), 0, wxBOTTOM, 3);
                auto* value = Label(row, "0 sats", 12, colour, true, wxST_ELLIPSIZE_END);
                value->SetMinSize(wxSize(70, -1));
                column->Add(value, 0, wxEXPAND);
                m_metrics[i]->Add(column, 1, wxEXPAND);
                return value;
            };
            m_pot[i] = metric("POT", TEXT);
            m_paid_out[i] = metric("PAID OUT", TEXT);
            m_pending[i] = metric("PENDING", MUTED);
            // Keep the timer with its amount, including network-send time.
            m_pending_delay[i] = Label(row, "", 8, MUTED);
            m_metrics[i]->GetItem(2)->GetSizer()->Add(m_pending_delay[i], 0, wxTOP, 2);
            m_body[i]->Add(m_metrics[i], 1, wxALIGN_CENTER_VERTICAL);
            info->Add(m_body[i], 0, wxEXPAND);
            m_pay[i] = new wxButton(row, wxID_ANY, "Pay from test wallet");
            m_pay[i]->SetName(wxString::Format("Pay Player %d test entry", i + 1));
            m_pay[i]->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) { Send(i < 2 ? 2 + i : 7 + i); });
            m_pay[i]->Hide();
            info->Add(m_pay[i], 0, wxTOP, 8);
            info->Add(Divider(row), 0, wxTOP | wxEXPAND, MARGIN);
            row->SetSizer(info);
            root->Add(row, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, MARGIN);
        }
        m_verification_note = Label(this, "* Receiving addresses checked automatically", 8, ACCENT);
        m_verification_note->SetToolTip("Successful receiving checks are reused for 30 minutes. Changed addresses, expired checks, or incompatible limits are checked again. Verification does not send funds.");
        root->Add(m_verification_note, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, MARGIN);
        m_error = Label(this, "", 9, ACCENT);
        root->Add(m_error, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, MARGIN);
        root->Add(Label(this, "Match activity", 10, TEXT, true), 0, wxLEFT | wxRIGHT | wxBOTTOM, MARGIN);
        auto* log_area = new wxPanel(this);
        log_area->SetBackgroundColour(SURFACE);
        auto* log_layout = new wxBoxSizer(wxVERTICAL);
        m_log = new wxTextCtrl(log_area, wxID_ANY, "No payments yet.", wxDefaultPosition, wxSize(-1, 140),
                              wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE);
        m_log->SetBackgroundColour(SURFACE);
        m_log->SetForegroundColour(wxColour(205, 208, 216));
        m_log->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        log_layout->Add(m_log, 1, wxALL | wxEXPAND, 8);
        log_area->SetSizer(log_layout);
        root->Add(log_area, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, MARGIN);
        m_show_outbound = new wxCheckBox(this, wxID_ANY, "Show outbound payments");
        m_show_outbound->SetForegroundColour(TEXT);
        m_show_outbound->SetToolTip("Debug all queued, in-flight, paid and failed payments, including earlier games. Select text to pause updates for copying.");
        root->Add(m_show_outbound, 0, wxLEFT | wxRIGHT | wxBOTTOM, MARGIN);
        m_outbound_area = new wxPanel(this);
        m_outbound_area->SetMinSize(wxSize(1, 276));
        m_outbound_area->SetBackgroundColour(SURFACE);
        auto* outbound_layout = new wxBoxSizer(wxVERTICAL);
        m_outbound = new wxTextCtrl(m_outbound_area, wxID_ANY, "Loading outbound payments...",
                                   wxDefaultPosition, wxSize(-1, 260),
                                   wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE);
        m_outbound->SetName("Outbound payments debug log");
        m_outbound->SetMinSize(wxSize(1, 260));
        m_outbound->SetBackgroundColour(SURFACE);
        m_outbound->SetForegroundColour(TEXT);
        m_outbound->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        outbound_layout->Add(m_outbound, 1, wxALL | wxEXPAND, 8);
        m_outbound_area->SetSizer(outbound_layout);
        root->Add(m_outbound_area, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, MARGIN);
        m_outbound_area->Hide();
        m_show_outbound->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
            const bool show = m_show_outbound->GetValue();
            arena_local_outbound_debug(m_handle, show);
            m_outbound_area->Show(show);
            m_outbound_refreshed = {};
            if (show) UpdateOutbound();
            LayoutContents();
        });
        m_help = Label(this, "", 8, MUTED);
        root->Add(m_help, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, MARGIN);
        SetSizer(root);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Poll(); });
        m_timer.Start(100);
        Poll();
    }

	~FundingPanel() override
	{
		m_timer.Stop();
		arena_local_outbound_debug(m_handle, false);
	}

private:
	void LayoutContents()
	{
		// Hidden GTK controls need an explicit virtual extent when expanded.
		SetVirtualSize(GetSizer()->CalcMin());
		Layout();
		if (m_outbound_area->IsShown()) m_outbound_area->Layout();
	}

	void UpdateOutbound()
	{
		if (!m_show_outbound->GetValue() || !m_outbound->GetStringSelection().empty()) return;
		const auto now = std::chrono::steady_clock::now();
		if (now - m_outbound_refreshed < std::chrono::seconds(1)) return;
		m_outbound_refreshed = now;
		auto required = arena_local_outbound_snapshot(m_handle, m_outbound_buffer.data(),
		                                            static_cast<std::uint32_t>(m_outbound_buffer.size()));
		if (required > m_outbound_buffer.size())
		{
			m_outbound_buffer.resize(required);
			required = arena_local_outbound_snapshot(m_handle, m_outbound_buffer.data(), required);
		}
		// A changing snapshot can grow between reads; retry on the next UI tick.
		if (!required || required > m_outbound_buffer.size()) return;
		const auto snapshot = wxString::FromUTF8(m_outbound_buffer.data());
		if (m_outbound->GetValue() != snapshot)
		{
			const int scroll = m_outbound->GetScrollPos(wxVERTICAL);
			m_outbound->ChangeValue(snapshot);
			m_outbound->SetScrollPos(wxVERTICAL, scroll);
		}
	}

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
	static bool Pays(const ArenaLocalView& view, int player)
	{
		return view.kinds[player] == 0 || (view.cpu_wallets && view.kinds[player] == 1);
	}
	void Poll()
	{
		if (!m_enabled) { m_phase->SetLabel("Payment gate disabled"); return; }
		ArenaLocalView view = {};
		if (!m_handle || !arena_local_snapshot(m_handle, &view)) return;
		if (view.abi_version != 10) { m_error->SetLabel("Payment engine ABI mismatch"); return; }
		UpdateOutbound();
		if (!m_addresses_loaded && view.phase != 0)
		{
			for (int i = 0; i < 4; ++i)
				if (!m_addresses_dirty[i]) m_address[i]->ChangeValue(wxString::FromUTF8(view.addresses[i]));
			m_addresses_loaded = true;
		}
		for (int i = 0; i < 4; ++i)
		{
			if (!m_address_pending[i] || view.busy) continue;
			// A receiving check must not overwrite edits made while it was in flight.
			const bool unchanged = m_address[i]->GetValue() == m_address_submitted[i];
			if (unchanged && view.verification_state[i] == 1 &&
				wxString::FromUTF8(view.addresses[i]) == m_address_submitted[i]) continue;
			m_address_pending[i] = false;
			if (!view.error[0] && unchanged)
			{
				m_addresses_dirty[i] = false;
			}
			if (!unchanged) {
				m_auto_attempted[i].clear();
				m_auto_failure[i].clear();
			} else if (!view.error[0] && view.verification_state[i] == 2) {
				m_auto_failure[i].clear();
			} else {
				m_auto_failure[i] = wxString::FromUTF8(view.error[0] ? view.error : view.verification_message[i]);
				if (m_auto_failure[i].empty()) m_auto_failure[i] = "Receiving address could not be verified";
			}
		}
		auto& gate = SatsArena::GetPaymentGate();
		const auto css = gate.Read();
		const auto match = SatsArena::GetTracker().Read();
		const bool roster_matches = SameRoster(view, css);
		const bool funding = view.phase == 1 || view.phase == 2;
		const auto now = std::chrono::steady_clock::now();
		if (funding && css.ready && roster_matches && !view.busy && !m_start_pending) {
			for (int i = 0; i < 4; ++i) {
				if (!Pays(view, i)) continue;
				const wxString input = m_address[i]->GetValue();
				if (m_address_pending[i] && input == m_address_submitted[i]) continue;
				const auto humans = std::count_if(std::begin(view.kinds), std::end(view.kinds),
				    [&view](unsigned kind) { return kind == 0 || (view.cpu_wallets && kind == 1); });
				const wxString key = input + wxString::Format("|%llu|%u", static_cast<unsigned long long>(view.entry_sats), static_cast<unsigned>(humans));
				if (view.verification_state[i] == 4 && now - m_auto_started[i] > std::chrono::minutes(9))
					m_auto_attempted[i].clear();
				const bool current = !m_addresses_dirty[i] && view.verification_state[i] == 2;
				if (current || input.empty() || key == m_auto_attempted[i] || now - m_edited_at[i] < std::chrono::milliseconds(750)) continue;
				if (arena_local_verify_address(m_handle, i, input.utf8_str().data())) {
					m_auto_attempted[i] = key;
					m_auto_started[i] = now;
					m_auto_failure[i].clear();
					m_address_pending[i] = true;
					m_address_submitted[i] = input;
				}
				break;
			}
		}
		const bool checking_addresses = std::any_of(std::begin(m_address_pending), std::end(m_address_pending), [](bool pending) { return pending; });
		bool addresses_saved = !checking_addresses;
		bool any_paid = false;
		for (int i = 0; i < 4; ++i)
		{
			if (Pays(view, i) && (m_addresses_dirty[i] || view.verification_state[i] != 2)) addresses_saved = false;
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
			if ((view.phase == 4 || view.phase == 6) && css.roster_valid)
			{
				m_settling = false;
				m_capture_failed = false;
				Send(1);
			}
			else if ((view.phase == 1 || view.phase == 2) && css.ready && !roster_matches)
			{
				std::array<std::uint32_t, 4> kinds, teams, characters;
				for (int i = 0; i < 4; ++i) { kinds[i] = css.kinds[i]; teams[i] = css.team_ids[i]; characters[i] = css.characters[i]; }
				arena_local_roster(m_handle, css.teams, kinds.data(), teams.data(), characters.data());
				m_lobby_generation = css.lobby_generation;
			}
			else if ((view.phase == 1 || view.phase == 2) && any_paid &&
				(!Core::IsRunning() || (m_lobby_generation && m_lobby_generation != css.lobby_generation)))
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
			else if (roster_matches) m_lobby_generation = css.lobby_generation;
		}
        const bool show_css = !m_admitted && !m_in_match && (css.kinds[0] < 2 || css.kinds[1] < 2 || css.kinds[2] < 2 || css.kinds[3] < 2);
        wxString phase;
        if (view.phase == 0) phase = "Connecting wallet...";
        else if (view.phase == 7 || view.error[0]) phase = "Payment needs attention";
        else if (view.phase == 6) phase = "Sending payouts...";
        else if (m_in_match) phase = "Match in progress";
        else if (m_admitted) phase = "Choose a stage";
        else if (funding && (!css.roster_valid || !roster_matches)) phase = "Select your fighters";
        else if (checking_addresses) phase = "Checking receiving addresses...";
        else if (funding && !addresses_saved) phase = "Check receiving addresses";
        else if (view.phase == 2) phase = css.requested ? "Starting match..." : "Ready • PRESS START";
        else if (view.phase == 4) phase = "Match settled";
        else phase = "Awaiting entry payments";
        m_phase->SetLabel(phase);
        m_phase->SetForegroundColour(view.error[0] || view.payment_error[0] || view.phase == 7 ? ACCENT : MUTED);
        m_phase->Wrap(CONTENT_WIDTH);
        wxString background;
        if (view.background_pending_sats)
            background = wxString::Format("Previous match payouts: %llu sats pending in the background.",
                static_cast<unsigned long long>(view.background_pending_sats));
        if (view.background_failed_sats) {
            if (!background.empty()) background += "\n";
            background += wxString::Format("Previous payouts needing attention: %llu sats.",
                static_cast<unsigned long long>(view.background_failed_sats));
        }
        if (!background.empty()) background += "\nYou can play a new match.";
        m_background_payments->SetLabel(background);
        m_background_payments->Wrap(CONTENT_WIDTH);
        m_background_payments->Show(!background.empty());
        wxString warning;
        if (funding && !addresses_saved && css.ready) {
            for (int i = 0; i < 4; ++i) {
                if (!Pays(view, i)) continue;
                if (!m_auto_failure[i].empty()) { warning = wxString::Format("Player %d: %s", i + 1, m_auto_failure[i]); break; }
                if (m_address[i]->GetValue().empty()) { warning = wxString::Format("Player %d: enter a Lightning address.", i + 1); break; }
            }
        }
        m_address_warning->SetLabel(warning);
        m_address_warning->Wrap(CONTENT_WIDTH);
        m_address_warning->Show(!warning.empty());
        const wxString entry_rule = view.phase == 0 ? wxString("...") : wxString::Format("%llu sats", static_cast<unsigned long long>(view.entry_sats));
        const wxString rate_rule = view.phase == 0 ? wxString("...") : wxString::Format("%llu %s / 1%%", static_cast<unsigned long long>(view.sats_per_percent), view.sats_per_percent == 1 ? "sat" : "sats");
        m_entry_rule->SetLabel(entry_rule);
        m_entry_rule->SetToolTip(entry_rule + " per player with a wallet");
        m_rate_rule->SetLabel(rate_rule);
        m_rate_rule->SetToolTip(rate_rule);
        m_invoice_rule->SetLabel(wxString::Format("Invoice: %llu sats • %llu pot + %llu fees", static_cast<unsigned long long>(view.invoice_sats), static_cast<unsigned long long>(view.entry_sats), static_cast<unsigned long long>(view.surcharge_sats)));
        m_invoice_rule->Wrap(CONTENT_WIDTH);
        m_invoice_rule->Show(view.phase != 0);
        m_wallet_mode->SetLabel(view.sandbox ? "SANDBOX • test payments" : "FAKE WALLET • simulated payments");
        m_wallet_mode->SetToolTip("Development wallet: no real bitcoin is transferred.");
        m_wallet_mode->Show(view.phase != 0 && (view.sandbox || view.simulated));
        m_help->SetLabel(wxString::Format("Damage pays in %llu-second batches.\nThe remaining pot settles after the match.", static_cast<unsigned long long>(view.batch_window_ms / 1000)));
        if (view.cpu_wallets) m_help->SetLabel(m_help->GetLabel() + "\nCPU wallets enabled for this match.");
        m_help->Wrap(CONTENT_WIDTH);
        m_verification_note->Show(funding);
        for (int i = 0; i < 4; ++i)
        {
            const unsigned kind = show_css ? css.kinds[i] : view.kinds[i];
            const unsigned team = show_css ? css.team_ids[i] : view.team_ids[i];
            const bool teams = show_css ? css.teams : bool(view.teams);
            const bool human = kind == 0;
            const bool financial = human || (view.cpu_wallets && kind == 1);
            const bool paid = view.paid_sats[i] != 0;
            // GTK can cache default-font sizes before a hidden player row is
            // realized. Re-measure on the tick after showing it, so P3/P4 have
            // the same text sizing as the initially visible rows.
            if (m_refresh_layout[i]) {
                for (auto* child : m_rows[i]->GetChildren()) child->InvalidateBestSize();
                m_rows[i]->InvalidateBestSize();
            }
            m_refresh_layout[i] = m_rows[i]->Show(kind < 2);
            wxString label = wxString::Format("PLAYER %d • %s", i + 1, human ? "Human" : "CPU");
            if (teams && team < 3) label = wxString::Format(human ? "PLAYER %d • %s Team" : "PLAYER %d • CPU • %s", i + 1, TeamName(team));
            m_player[i]->SetLabel(label);
            m_entry_status[i]->SetLabel(!financial ? "NO ENTRY" : (paid ? "PAID" : "UNPAID"));
            m_entry_status[i]->SetForegroundColour(!financial ? MUTED : (paid ? GREEN : ACCENT));
            m_entry_status[i]->SetToolTip(financial ? wxString::Format("%s: %llu sats", paid ? "Entry received" : "Entry to pay", static_cast<unsigned long long>(view.invoice_sats)) : "CPU wallets are disabled for this match");
            m_address[i]->Show(financial);
            m_verify[i]->Show(financial && funding && !m_auto_failure[i].empty());
            // Keep focus, selection and caret intact while asynchronous validation runs.
            m_address[i]->Enable(financial && funding && !any_paid && !view.sandbox);
            m_verify[i]->Enable(funding && roster_matches && !view.busy && !m_address_pending[i]);
            const unsigned status = m_addresses_dirty[i] ? 0 : view.verification_state[i];
            wxString verification;
            wxColour colour = MUTED;
            if (!financial) verification = "CPU • no payments";
            else if (m_address_pending[i]) verification = "Checking receiver...";
            else if (!m_auto_failure[i].empty()) { verification = m_auto_failure[i]; colour = ACCENT; }
            else if (status == 2) { verification = "Verified *"; colour = GREEN; }
            else if (status == 3 || status == 5) { verification = wxString::FromUTF8(view.verification_message[i]); colour = ACCENT; }
            else verification = m_address[i]->GetValue().empty() ? "Enter a Lightning address" : "Waiting for automatic check...";
            m_verification[i]->SetLabel(verification);
            m_verification[i]->Wrap(CONTENT_WIDTH);
            m_verification[i]->SetForegroundColour(colour);
            m_verification[i]->SetToolTip(status == 2 ? "Receiving address verified automatically; reused for up to 30 minutes. No funds sent for verification." : wxString::FromUTF8(view.verification_message[i]));
            const bool show = financial && css.ready && roster_matches && addresses_saved && !m_admitted && funding;
            const wxString invoice = show && !paid && view.invoices[i][0] ? wxString::FromUTF8(view.invoices[i]) : "";
            const bool has_qr = !invoice.empty();
            m_body[i]->ShowItems(financial);
            if (invoice != m_last_invoice[i]) {
                m_qr[i]->SetBitmap(InvoiceQR(invoice.utf8_str().data()));
                m_qr[i]->SetMinSize(wxDefaultSize);
                m_qr[i]->InvalidateBestSize();
                m_last_invoice[i] = invoice;
            }
            m_qr[i]->Show(has_qr);
            if (has_qr != m_has_qr[i]) {
                if (has_qr) { m_metrics[i]->SetRows(3); m_metrics[i]->SetCols(1); }
                else { m_metrics[i]->SetCols(3); m_metrics[i]->SetRows(1); }
                m_has_qr[i] = has_qr;
            }
            m_pot[i]->SetLabel(wxString::Format("%llu sats", static_cast<unsigned long long>(view.remaining_sats[i])));
            m_paid_out[i]->SetLabel(wxString::Format("%llu sats", static_cast<unsigned long long>(view.earned_sats[i])));
            m_paid_out[i]->SetForegroundColour(view.earned_sats[i] ? GREEN : TEXT);
            m_pending[i]->SetLabel(wxString::Format("%llu sats", static_cast<unsigned long long>(view.pending_sats[i])));
            m_pending[i]->SetForegroundColour(view.pending_sats[i] ? ACCENT : MUTED);
            m_pending_delay[i]->SetLabel(view.batch_remaining_ms[i] ? wxString::Format("in %llus", static_cast<unsigned long long>((view.batch_remaining_ms[i] + 999) / 1000)) : wxString("Sending..."));
            m_pending_delay[i]->Show(financial && view.pending_sats[i] != 0);
            m_pay[i]->SetLabel(view.sandbox ? "Pay from test wallet" : "Simulate payment");
            m_pay[i]->Show(financial && !paid && (view.simulated || view.sandbox) && funding);
            m_pay[i]->Enable(show && view.phase == 1 && !view.busy && !paid);
            m_rows[i]->Layout();
        }

		m_error->SetLabel(wxString::FromUTF8(view.error[0] ? view.error : view.payment_error));
		m_error->Wrap(CONTENT_WIDTH);
		m_error->Show(view.error[0] != 0 || view.payment_error[0] != 0);
		wxString activity;
		if ((m_admitted || m_in_match) && (m_capture_failed || match.credit_stream_incomplete)) activity = "Incomplete match data • unspent funds will be returned\n";
		activity += wxString::FromUTF8(view.payment_log);
		if (activity.empty()) activity = "No payments yet.";
		if (m_log->GetValue() != activity) { m_log->ChangeValue(activity); m_log->ShowPosition(m_log->GetLastPosition()); }
		LayoutContents();
	}

	ArenaLocalHandle* m_handle = nullptr;
	wxTimer m_timer;
	bool m_enabled = true, m_start_pending = false, m_admitted = false;
	bool m_in_match = false, m_abandon = false, m_settling = false;
	std::uint64_t m_generation = 0, m_match_number = 0, m_log_revision = ~std::uint64_t(0);
	std::uint64_t m_lobby_generation = 0;
	bool m_capture_failed = false;
	bool m_credit_pending = false;
	wxStaticText* m_invoice_rule = nullptr;
	wxStaticText* m_entry_rule = nullptr;
	wxStaticText* m_rate_rule = nullptr;
	wxStaticText* m_wallet_mode = nullptr;
	wxStaticText* m_help = nullptr;
	wxStaticText* m_phase = nullptr;
	wxStaticText* m_background_payments = nullptr;
	wxStaticText* m_error = nullptr;
	wxPanel* m_rows[4] = {};
    bool m_refresh_layout[4] = {};
	wxStaticText* m_player[4] = {};
	wxStaticText* m_entry_status[4] = {};
	wxStaticText* m_pot[4] = {};
	wxStaticText* m_paid_out[4] = {};
	wxStaticText* m_pending[4] = {};
	wxStaticText* m_pending_delay[4] = {};
	wxBoxSizer* m_body[4] = {};
	wxGridSizer* m_metrics[4] = {};
	bool m_has_qr[4] = {};
	wxStaticText* m_verification[4] = {};
	wxStaticText* m_verification_note = nullptr;
	wxStaticBitmap* m_qr[4] = {};
	wxTextCtrl* m_address[4] = {};
	wxButton* m_verify[4] = {};
	wxStaticText* m_address_warning = nullptr;
	bool m_addresses_loaded = false, m_addresses_dirty[4] = {};
	bool m_address_pending[4] = {};
	wxString m_address_submitted[4];
	wxString m_auto_attempted[4], m_auto_failure[4];
	std::chrono::steady_clock::time_point m_edited_at[4] = {}, m_auto_started[4] = {};
	wxTextCtrl* m_log = nullptr;
	wxCheckBox* m_show_outbound = nullptr;
	wxPanel* m_outbound_area = nullptr;
	wxTextCtrl* m_outbound = nullptr;
	std::vector<char> m_outbound_buffer = std::vector<char>(4096);
	std::chrono::steady_clock::time_point m_outbound_refreshed = {};
	wxButton* m_pay[4] = {};
	wxString m_last_invoice[4];
};
}
wxWindow* CreateSidebar(wxWindow* parent) { return new FundingPanel(parent); }
}
