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
#include <wx/frame.h>
#include <wx/hyperlink.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/tglbtn.h>

#include "Common/FileUtil.h"
#include "Core/Core.h"
#include "Core/Slippi/SatsArenaTracker.h"
#include "DolphinWX/QR/qrcodegen.hpp"
#include "sats_arena_local.h"
static_assert(sizeof(ArenaLocalView) == 19144, "Arena local ABI layout mismatch");

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
const int CONTENT_WIDTH = 460;
const int PLAYER_WIDTH = 224;
const int MARGIN = 6;

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
    // BOLT11 accepts uppercase, which uses fewer QR modules and keeps them legible.
    const auto qr_text = wxString::FromUTF8(invoice).Upper();
    const auto qr = qrcodegen::QrCode::encodeText(qr_text.utf8_str().data(), qrcodegen::QrCode::Ecc::MEDIUM);
    const int modules = qr.getSize();
    const int quiet = 4;
    const int scale = std::max(1, 148 / (modules + quiet * 2));
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

class FundingPanel final : public wxPanel
{
public:
    explicit FundingPanel(wxWindow* parent)
        : wxPanel(parent, wxID_ANY), m_timer(this)
    {
        SetBackgroundColour(BACKGROUND);
        SetForegroundColour(TEXT);
        SetName("Super Sats Bro Melee match funding");
        const char* env = std::getenv("SATS_ARENA_FAKE_LOCAL");
        m_enabled = !env || std::strcmp(env, "0") != 0;
        SatsArena::GetPaymentGate().Enable(m_enabled);
        m_handle = m_enabled ? GetHost().Get() : nullptr;
        auto* root = new wxBoxSizer(wxVERTICAL);
        auto* header = new wxBoxSizer(wxHORIZONTAL);
        header->Add(Label(this, "Sats Melee", 12, TEXT, true), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, MARGIN);
        m_phase = Label(this, "Connecting wallet...", 10, MUTED);
        header->Add(m_phase, 1, wxALIGN_CENTER_VERTICAL);
        m_wallet_mode = Label(this, "", 8, ACCENT);
        header->Add(m_wallet_mode, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 6);
        m_wallet_mode->Hide();
        root->Add(header, 0, wxALL | wxEXPAND, MARGIN);
        m_phone_room = Label(this, "", 10, ACCENT, true);
        m_phone_room->SetName("Phone room connection");
        root->Add(m_phone_room, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, MARGIN);
        m_phone_room->Hide();
        m_background_payments = Label(this, "", 9, MUTED);
        root->Add(m_background_payments, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, MARGIN);
        m_background_payments->Hide();

        auto* rules = new wxBoxSizer(wxHORIZONTAL);
        auto* entry_rule = new wxBoxSizer(wxHORIZONTAL);
        entry_rule->Add(Label(this, "Entry / player", 8, MUTED), 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 6);
        m_entry_rule = Label(this, "...", 10, TEXT, true, wxST_ELLIPSIZE_END);
        m_entry_rule->SetMinSize(wxSize(100, -1));
        entry_rule->Add(m_entry_rule, 0, wxEXPAND);
        rules->Add(entry_rule, 1, wxRIGHT, 10);
        auto* rate_rule = new wxBoxSizer(wxHORIZONTAL);
        rate_rule->Add(Label(this, "Damage", 8, MUTED), 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 6);
        m_rate_rule = Label(this, "...", 10, ACCENT, true, wxST_ELLIPSIZE_END);
        m_rate_rule->SetMinSize(wxSize(100, -1));
        rate_rule->Add(m_rate_rule, 0, wxEXPAND);
        rules->Add(rate_rule, 1);
        root->Add(rules, 0, wxLEFT | wxRIGHT | wxEXPAND, MARGIN);
        m_invoice_rule = Label(this, "", 9, MUTED);
        m_invoice_rule->SetToolTip("The routing contribution is added to the invoice and retained by the host wallet for fees, including after cancellation. It is separate from the playable pot.");
        root->Add(m_invoice_rule, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, MARGIN);
        m_address_warning = Label(this, "", 9, ACCENT);
        root->Add(m_address_warning, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, MARGIN);
        m_address_warning->Hide();
        root->Add(Divider(this), 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, MARGIN);

        m_summary = new wxPanel(this);
        auto* summary_layout = new wxBoxSizer(wxVERTICAL);
        m_summary_title = Label(m_summary, "Last match", 14, TEXT, true);
        summary_layout->Add(m_summary_title, 0, wxBOTTOM | wxEXPAND, 12);
        summary_layout->Add(Label(m_summary, "Damage dealt (%)", 10, TEXT, true), 0, wxBOTTOM | wxEXPAND, 12);
        m_summary_grid = new wxPanel(m_summary);
        summary_layout->Add(m_summary_grid, 0, wxEXPAND | wxBOTTOM, 12);
        m_summary_note = Label(m_summary, "", 9, MUTED);
        summary_layout->Add(m_summary_note, 0, wxBOTTOM | wxEXPAND, 12);
        summary_layout->Add(Label(m_summary, "Press Start to continue.", 9, MUTED), 0, wxEXPAND);
        m_summary->SetSizer(summary_layout);
        root->Add(m_summary, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, MARGIN);
        m_summary->Hide();

        for (int i = 0; i < 4; ++i)
        {
            if (i % 2 == 0) {
                m_pairs[i / 2] = new wxPanel(this);
                m_pairs[i / 2]->SetSizer(new wxBoxSizer(wxHORIZONTAL));
                root->Add(m_pairs[i / 2], 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 6);
            }
            auto* row = new wxPanel(m_pairs[i / 2]);
            m_rows[i] = row;
            row->SetBackgroundColour(BACKGROUND);
            row->SetForegroundColour(TEXT);
            auto* info = new wxBoxSizer(wxVERTICAL);
            auto* heading = new wxBoxSizer(wxHORIZONTAL);
            m_player[i] = Label(row, wxString::Format("PLAYER %d • Human", i + 1), 10, TEXT, true, wxST_ELLIPSIZE_END);
            m_player[i]->SetMinSize(wxSize(80, -1));
            heading->Add(m_player[i], 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
            m_entry_status[i] = Label(row, "UNPAID", 10, ACCENT, true);
            heading->Add(m_entry_status[i], 0, wxALIGN_CENTER_VERTICAL);
            info->Add(heading, 0, wxEXPAND | wxBOTTOM, 3);

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
            m_address[i]->Bind(wxEVT_KILL_FOCUS, [this, i](wxFocusEvent& event) {
                m_editing_address[i] = false;
                event.Skip();
            });
            m_address[i]->Bind(wxEVT_SET_FOCUS, [this, i](wxFocusEvent& event) {
                m_editing_address[i] = true;
                event.Skip();
            });
            m_address[i]->Bind(wxEVT_CHAR_HOOK, [this, i](wxKeyEvent& event) {
                if (event.GetKeyCode() != WXK_TAB || event.ControlDown() || event.AltDown()) {
                    event.Skip();
                    return;
                }
                const int step = event.ShiftDown() ? -1 : 1;
                for (int next = i + step; next >= 0 && next < 4; next += step) {
                    if (m_rows[next]->IsShown() && m_address[next]->IsEnabled()) {
                        EditAddress(next);
                        return;
                    }
                }
                if (!event.ShiftDown()) m_show_activity->SetFocus();
                else event.Skip();
            });
            auto* address_row = new wxBoxSizer(wxHORIZONTAL);
            address_row->Add(m_address[i], 1, wxRIGHT | wxALIGN_CENTER_VERTICAL, 6);
            m_address_display[i] = Label(row, "", 8, MUTED, false, wxST_ELLIPSIZE_END);
            m_address_display[i]->SetMinSize(wxSize(80, -1));
            address_row->Add(m_address_display[i], 1, wxRIGHT | wxALIGN_CENTER_VERTICAL, 6);
            m_edit_address[i] = new wxHyperlinkCtrl(row, wxID_ANY, "Edit", "");
            m_edit_address[i]->SetName(wxString::Format("Edit Player %d receiving address", i + 1));
            m_edit_address[i]->SetNormalColour(MUTED);
            m_edit_address[i]->SetVisitedColour(MUTED);
            m_edit_address[i]->SetHoverColour(TEXT);
            m_edit_address[i]->Bind(wxEVT_HYPERLINK, [this, i](wxHyperlinkEvent&) {
                EditAddress(i);
            });
            address_row->Add(m_edit_address[i], 0, wxALIGN_CENTER_VERTICAL);
            m_verify[i] = new wxButton(row, wxID_ANY, "Retry", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
            m_verify[i]->SetName(wxString::Format("Retry Player %d receiving address", i + 1));
            m_verify[i]->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) {
                m_auto_attempted[i].clear();
                m_auto_failure[i].clear();
            });
            address_row->Add(m_verify[i], 0);
            info->Add(address_row, 0, wxBOTTOM | wxEXPAND, 2);
            m_verification[i] = Label(row, "Not verified", 8, MUTED);
            m_verification[i]->SetMinSize(wxSize(40, -1));

            m_body[i] = new wxBoxSizer(wxHORIZONTAL);
            m_qr[i] = new wxStaticBitmap(row, wxID_ANY, InvoiceQR(""));
            m_qr[i]->SetName(wxString::Format("Player %d payment QR code", i + 1));
            m_body[i]->Add(m_qr[i], 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 8);
            m_qr[i]->Hide();
            m_metrics[i] = new wxGridSizer(1, 3, 4, 4);
            auto metric = [&](const char* caption, const wxColour& colour) {
                auto* column = new wxBoxSizer(wxVERTICAL);
                column->Add(Label(row, caption, 8, MUTED), 0, wxBOTTOM, 3);
                auto* value = Label(row, "0 sats", 10, colour, true, wxST_ELLIPSIZE_END);
                value->SetMinSize(wxSize(68, -1));
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
            auto* figures = new wxBoxSizer(wxVERTICAL);
            figures->Add(m_metrics[i], 0, wxEXPAND);
            m_body[i]->Add(figures, 1, wxALIGN_CENTER_VERTICAL);
            info->Add(m_body[i], 0, wxEXPAND);
            m_pay[i] = new wxButton(row, wxID_ANY, "Test pay", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
            m_pay[i]->SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
            m_pay[i]->SetName(wxString::Format("Pay Player %d test entry", i + 1));
            m_pay[i]->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) { Send(i < 2 ? 2 + i : 7 + i); });
            m_pay[i]->Hide();
            figures->Add(m_pay[i], 0, wxTOP, 4);
            info->Add(m_verification[i], 0, wxTOP | wxEXPAND, 4);
            info->Add(Divider(row), 0, wxTOP | wxEXPAND, 6);
            row->SetSizer(info);
            if (i % 2) m_pairs[i / 2]->GetSizer()->AddSpacer(12);
            m_pairs[i / 2]->GetSizer()->Add(row, 1, wxEXPAND | wxRESERVE_SPACE_EVEN_IF_HIDDEN);
        }
        m_error = Label(this, "", 9, ACCENT, false, wxST_ELLIPSIZE_END);
        m_error->SetMinSize(wxSize(80, -1));
        root->Add(m_error, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, MARGIN);
        auto* log_toggles = new wxBoxSizer(wxHORIZONTAL);
        m_show_activity = new wxCheckBox(this, wxID_ANY, "Match activity");
        m_show_activity->SetForegroundColour(TEXT);
        log_toggles->Add(m_show_activity, 0, wxRIGHT, MARGIN);
        m_show_outbound = new wxCheckBox(this, wxID_ANY, "Outbound payments");
        m_show_outbound->SetForegroundColour(TEXT);
        m_show_outbound->SetToolTip("Debug all queued, in-flight, paid and failed payments, including earlier games. Select text to pause updates for copying.");
        log_toggles->Add(m_show_outbound, 0, wxRIGHT, 8);
        m_show_missed = new wxCheckBox(this, wxID_ANY, "Missed");
        m_show_missed->SetForegroundColour(TEXT);
        m_show_missed->SetToolTip("Amounts still owed, receiving addresses, and which payments are safe to request after the match.");
        log_toggles->Add(m_show_missed, 0);
        m_last_match = new wxToggleButton(this, wxID_ANY, "Last match", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        m_last_match->SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        m_last_match->Enable(false);
        log_toggles->AddStretchSpacer();
        log_toggles->Add(m_last_match, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 6);
        m_last_match->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) {
            m_summary_manual = m_last_match->GetValue();
            m_summary_auto = false;
            m_summary_start_request = SatsArena::GetPaymentGate().Read().start_requests;
            LayoutSummary();
        });
        root->Add(log_toggles, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, MARGIN);
        m_discard = new wxButton(this, wxID_ANY, "Discard unsent payments");
        m_discard->Enable(false);
        m_discard->SetToolTip("Discard queued payouts and open damage batches from this and earlier matches. Funds stay in the host wallet. Submitted Lightning payments cannot be cancelled.");
        m_discard->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            wxMessageDialog confirm(this,
                wxString::Format("Discard up to %llu sats in unsent payments?\n\nThese funds will stay in the host wallet and will not be paid to players. Already-submitted payments may still complete. Payment history is kept.",
                    static_cast<unsigned long long>(m_unsent_sats)),
                "Discard unsent payments", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
            confirm.SetYesNoLabels("Discard unsent", "Keep payments");
            if (confirm.ShowModal() == wxID_YES) Send(14);
        });
        m_pay_missed = new wxButton(this, wxID_ANY, "Pay missed payments");
        m_pay_missed->Enable(false);
        m_pay_missed->SetToolTip("After match end, combine safe missed amounts by address. Unknown original sends are checked separately and never paid twice.");
        m_pay_missed->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            wxMessageDialog confirm(this,
                wxString::Format("Pay %llu sats in missed payments?\n\nSafe amounts from finished matches are combined by receiving address. Unconfirmed original sends are excluded. Routing fees follow your wallet policy.",
                    static_cast<unsigned long long>(m_payable_missed_sats)),
                "Pay missed payments", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
            confirm.SetYesNoLabels("Pay missed", "Cancel");
            if (confirm.ShowModal() == wxID_YES) Send(16);
        });
        auto* payment_actions = new wxBoxSizer(wxHORIZONTAL);
        payment_actions->Add(m_discard, 0, wxRIGHT, MARGIN);
        payment_actions->Add(m_pay_missed, 0);
        root->Add(payment_actions, 0, wxLEFT | wxRIGHT | wxBOTTOM, MARGIN);
        m_log_window = new wxFrame(this, wxID_ANY, "Sats Melee logs", wxDefaultPosition, wxSize(760, 360),
                                   wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT);
        m_logs = new wxPanel(m_log_window);
        m_logs->SetBackgroundColour(BACKGROUND);
        auto* logs_layout = new wxBoxSizer(wxHORIZONTAL);
        m_log_area = new wxPanel(m_logs);
        m_log_area->SetBackgroundColour(SURFACE);
        auto* log_layout = new wxBoxSizer(wxVERTICAL);
        m_log = new wxTextCtrl(m_log_area, wxID_ANY, "No payments yet.", wxDefaultPosition, wxSize(1, 1),
                              wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE);
        m_log->SetName("Match activity log");
        m_log->SetMinSize(wxSize(1, 1));
        m_log->SetBackgroundColour(SURFACE);
        m_log->SetForegroundColour(wxColour(205, 208, 216));
        m_log->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        log_layout->Add(m_log, 1, wxALL | wxEXPAND, 8);
        m_log_area->SetSizer(log_layout);
        logs_layout->Add(m_log_area, 1, wxEXPAND);
        m_log_area->Hide();
        m_outbound_area = new wxPanel(m_logs);
        m_outbound_area->SetBackgroundColour(SURFACE);
        auto* outbound_layout = new wxBoxSizer(wxVERTICAL);
        m_outbound = new wxTextCtrl(m_outbound_area, wxID_ANY, "Loading outbound payments...",
                                   wxDefaultPosition, wxSize(1, 1),
                                   wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE);
        m_outbound->SetName("Outbound payments debug log");
        m_outbound->SetMinSize(wxSize(1, 1));
        m_outbound->SetBackgroundColour(SURFACE);
        m_outbound->SetForegroundColour(TEXT);
        m_outbound->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        outbound_layout->Add(m_outbound, 1, wxALL | wxEXPAND, 8);
        m_outbound_area->SetSizer(outbound_layout);
        logs_layout->Add(m_outbound_area, 1, wxEXPAND);
        m_outbound_area->Hide();
        m_missed_area = new wxPanel(m_logs);
        m_missed_area->SetBackgroundColour(SURFACE);
        auto* missed_layout = new wxBoxSizer(wxVERTICAL);
        m_missed = new wxTextCtrl(m_missed_area, wxID_ANY, "No missed payments.", wxDefaultPosition,
                                 wxSize(1, 1), wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE);
        m_missed->SetName("Missed payments log");
        m_missed->SetMinSize(wxSize(1, 1));
        m_missed->SetBackgroundColour(SURFACE);
        m_missed->SetForegroundColour(TEXT);
        missed_layout->Add(m_missed, 1, wxALL | wxEXPAND, 8);
        m_missed_area->SetSizer(missed_layout);
        logs_layout->Add(m_missed_area, 1, wxEXPAND);
        m_missed_area->Hide();
        m_logs->SetSizer(logs_layout);
        auto* log_window_layout = new wxBoxSizer(wxVERTICAL);
        log_window_layout->Add(m_logs, 1, wxALL | wxEXPAND, MARGIN);
        m_log_window->SetSizer(log_window_layout);
        m_log_window->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
            if (!event.CanVeto()) { event.Skip(); return; }
            m_show_activity->SetValue(false);
            m_show_outbound->SetValue(false);
            m_show_missed->SetValue(false);
            m_missed_area->Hide();
            arena_local_outbound_debug(m_handle, false);
            m_log_area->Hide();
            m_outbound_area->Hide();
            LayoutContents();
            event.Veto();
        });
        m_show_activity->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
            m_log_area->Show(m_show_activity->GetValue());
            LayoutContents();
        });
        m_show_outbound->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
            const bool show = m_show_outbound->GetValue();
            arena_local_outbound_debug(m_handle, show);
            m_outbound_area->Show(show);
            m_outbound_refreshed = {};
            if (show) UpdateOutbound();
            LayoutContents();
        });
        m_show_missed->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
            m_missed_area->Show(m_show_missed->GetValue());
            LayoutContents();
        });
        SetSizer(root);
        Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
            if (event.GetKeyCode() == WXK_ESCAPE && m_summary->IsShown()) {
                m_summary_manual = m_summary_auto = false;
                LayoutSummary();
            } else event.Skip();
        });
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Poll(); });
        m_timer.Start(100);
        Poll();
    }

	~FundingPanel() override
	{
		m_timer.Stop();
		arena_local_outbound_debug(m_handle, false);
		m_log_window->Destroy();
	}

private:
    void BuildSummary(const ArenaLocalView& view)
    {
        wxString title = "No contest";
        if (view.summary_winner >= 0 && view.summary_winner < 4) {
            title = view.summary_teams ? wxString::Format("%s team wins", TeamName(view.summary_team_ids[view.summary_winner])) :
                wxString::Format("P%d wins", view.summary_winner + 1);
        }
        m_summary_title->SetLabel(title);
        if (m_summary_grid->GetSizer()) m_summary_grid->GetSizer()->Clear(true);
        std::vector<int> players;
        for (int p = 0; p < 4; ++p) if (view.summary_kinds[p] < 2) players.push_back(p);
        auto* grid = new wxFlexGridSizer(static_cast<int>(players.size()) + 1, 8, 8);
        for (std::size_t col = 1; col <= players.size(); ++col) grid->AddGrowableCol(static_cast<int>(col), 1);
        grid->Add(Label(m_summary_grid, "To →", 8, MUTED), 0, wxALIGN_CENTER_VERTICAL);
        for (int p : players) {
            grid->Add(Label(m_summary_grid, wxString::Format("P%d", p + 1), 10, TEXT, true), 0, wxALIGN_CENTER);
        }
        for (int attacker : players) {
            grid->Add(Label(m_summary_grid, wxString::Format(view.summary_kinds[attacker] == 1 ? "P%d CPU" : "P%d", attacker + 1), 9, TEXT, true), 0, wxALIGN_CENTER_VERTICAL);
            for (int victim : players) {
                const auto centi = view.summary_damage_centi[attacker][victim];
                const wxString value = attacker == victim ? wxString("—") :
                    (!view.summary_complete && centi == 0 ? wxString("?") : wxString::Format("%llu.%02llu%%",
                        static_cast<unsigned long long>(centi / 100), static_cast<unsigned long long>(centi % 100)));
                auto* cell = Label(m_summary_grid, value, 10, attacker == victim ? MUTED : TEXT);
                cell->SetToolTip(wxString::Format("P%d → P%d: %s", attacker + 1, victim + 1, value));
                grid->Add(cell, 0, wxALIGN_CENTER);
            }
        }
        m_summary_grid->SetSizer(grid);
        m_summary_note->SetLabel(view.summary_complete ?
            "Rows dealt damage to columns, across all stocks.\nDamage totals are separate from sats paid." :
            "Partial tracking. Some damage is unavailable (?);\nthese totals may be incomplete.");
        m_summary_note->SetForegroundColour(view.summary_complete ? MUTED : ACCENT);
        m_summary_note->Wrap(CONTENT_WIDTH);
        m_summary_grid->SetMinSize(wxSize(-1, grid->CalcMin().y));
        m_summary->SetMinSize(wxSize(-1, m_summary->GetSizer()->CalcMin().y));
        m_summary->Layout();
    }

    void LayoutSummary()
    {
        const bool show = m_summary_available && (m_summary_auto || m_summary_manual);
        m_summary->Show(show);
        m_last_match->SetValue(show);
        for (int pair = 0; pair < 2; ++pair)
            m_pairs[pair]->Show(!show && (m_rows[pair * 2]->IsShown() || m_rows[pair * 2 + 1]->IsShown()));
        LayoutContents();
    }

    void EditAddress(int player)
    {
        m_editing_address[player] = true;
        m_address_display[player]->Hide();
        m_edit_address[player]->Hide();
        m_address[player]->Show();
        m_rows[player]->Layout();
        LayoutContents();
        m_address[player]->SetFocus();
    }

	void LayoutContents()
	{
		m_log_window->Show(m_show_activity->GetValue() || m_show_outbound->GetValue() || m_show_missed->GetValue());
		Layout();
		m_log_window->Layout();
		m_logs->Layout();
		if (m_log_area->IsShown()) m_log_area->Layout();
		if (m_outbound_area->IsShown()) m_outbound_area->Layout();
        if (m_missed_area->IsShown()) m_missed_area->Layout();
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
		if (view.abi_version != 13) { m_error->SetLabel("Payment engine ABI mismatch"); return; }
        char room_status[512] = {};
        if (arena_local_room_status(m_handle, room_status, sizeof(room_status))) {
            const auto status = wxString::FromUTF8(room_status);
            if (m_phone_room_status != status) {
                m_phone_room_status = status;
                m_phone_room->SetLabel(status);
                m_phone_room->Wrap(CONTENT_WIDTH);
                m_phone_room->Show(!status.empty());
                Layout();
            }
        }
		m_unsent_sats = view.unsent_sats;
		m_discard->Enable(view.unsent_sats != 0 && !view.busy);
        m_payable_missed_sats = view.payable_missed_sats;
        m_pay_missed->Enable(view.payable_missed_sats > 0 && !view.busy);
        m_show_missed->SetForegroundColour(view.missed_sats ? ACCENT : TEXT);
        m_show_missed->SetToolTip(wxString::Format("%llu sats missed; %llu sats safe to request after match. Open for amounts, addresses, and status.",
            static_cast<unsigned long long>(view.missed_sats), static_cast<unsigned long long>(view.payable_missed_sats)));
        if (m_missed->GetStringSelection().empty()) {
            const auto text = wxString::FromUTF8(view.missed_log);
            if (m_missed->GetValue() != text) m_missed->ChangeValue(text);
        }
		UpdateOutbound();
		if (!m_addresses_loaded && view.phase != 0)
		{
			for (int i = 0; i < 4; ++i)
				if (!m_addresses_dirty[i]) m_address[i]->ChangeValue(wxString::FromUTF8(view.addresses[i]));
			m_addresses_loaded = true;
		}
		for (int i = 0; i < 4; ++i)
		{
			// Phone submissions use the same worker; do not overwrite an in-progress local edit.
			if (!m_addresses_dirty[i] && !m_address_pending[i] && !m_address[i]->HasFocus() &&
				m_address[i]->GetValue() != wxString::FromUTF8(view.addresses[i])) {
				m_address[i]->ChangeValue(wxString::FromUTF8(view.addresses[i]));
				m_auto_attempted[i].clear();
				m_auto_failure[i].clear();
			}
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
		if (funding && !view.busy && !m_start_pending) {
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
				const bool incomplete = m_capture_failed || match.credit_stream_incomplete || !match.credit_stream_seen;
				const unsigned result = winner >= 0 && winner < 4 ? (winner < 2 ? 6 + winner : 9 + winner) : (incomplete ? 13 : 8);
				if (Send(result)) { m_in_match = false; m_admitted = false; m_show_next_summary = true; }
			}
			else if (m_abandon || (!m_admitted && !m_start_pending) ||
				(m_admitted && !m_in_match && (!Core::IsRunning() || css.generation != m_generation)))
			{
				if (Send(8)) { m_abandon = false; m_admitted = false; }
			}
		}
		else if (!view.busy && !m_admitted && !m_start_pending)
		{
			if ((view.phase == 4 || view.phase == 6) && css.roster_valid)
			{
				m_capture_failed = false;
				Send(1);
			}
			else if ((view.phase == 1 || view.phase == 2) && css.ready && !roster_matches)
			{
				bool keeps_funding_slots = true;
				for (int i = 0; i < 4; ++i)
					if ((view.kinds[i] == 0 && css.kinds[i] == 3) ||
						(view.paid_sats[i] && (view.kinds[i] != css.kinds[i] ||
						view.team_ids[i] != css.team_ids[i] || bool(view.teams) != css.teams))) keeps_funding_slots = false;
				// Other room players may still be choosing or funding their fighters.
				if (keeps_funding_slots || css.requested) {
					std::array<std::uint32_t, 4> kinds, teams, characters;
					for (int i = 0; i < 4; ++i) { kinds[i] = css.kinds[i]; teams[i] = css.team_ids[i]; characters[i] = css.characters[i]; }
					arena_local_roster(m_handle, css.teams, kinds.data(), teams.data(), characters.data());
				}
			}
			else if ((view.phase == 1 || view.phase == 2) && any_paid &&
				(!Core::IsRunning() || (m_lobby_generation && m_lobby_generation != css.scene_generation)))
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
		}
		if (funding && !any_paid) m_lobby_generation = css.scene_generation;
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
            background = wxString::Format("Previous match payouts: %llu sats pending.",
                static_cast<unsigned long long>(view.background_pending_sats));
        if (view.background_failed_sats) {
            if (!background.empty()) background += "\n";
            background += wxString::Format("Previous payouts needing attention: %llu sats.",
                static_cast<unsigned long long>(view.background_failed_sats));
        }
        if (!background.empty()) background += " Play on.";
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
        m_rate_rule->SetToolTip(wxString::Format("%s, paid in %llu-second batches. The remaining pot settles after the match.", rate_rule, static_cast<unsigned long long>(view.batch_window_ms / 1000)));
        m_invoice_rule->SetLabel(wxString::Format("Invoice: %llu sats • %llu pot + %llu fees", static_cast<unsigned long long>(view.invoice_sats), static_cast<unsigned long long>(view.entry_sats), static_cast<unsigned long long>(view.surcharge_sats)));
        m_invoice_rule->Wrap(CONTENT_WIDTH);
        m_invoice_rule->Show(view.phase != 0);
        m_wallet_mode->SetLabel(view.sandbox ? "SANDBOX" : "FAKE WALLET");
        m_wallet_mode->SetToolTip("Development wallet: no real bitcoin is transferred.");
        m_wallet_mode->Show(view.phase != 0 && (view.sandbox || view.simulated));
        for (int i = 0; i < 4; ++i)
        {
            const bool use_css = show_css && !(funding && view.paid_sats[i]);
            const unsigned kind = use_css ? css.kinds[i] : view.kinds[i];
            const unsigned team = use_css ? css.team_ids[i] : view.team_ids[i];
            const bool teams = use_css ? css.teams : bool(view.teams);
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
            const unsigned status = m_addresses_dirty[i] ? 0 : view.verification_state[i];
            const bool compact_address = status == 2 && !m_editing_address[i] && !m_addresses_dirty[i] && !m_address[i]->HasFocus();
            m_address[i]->Show(financial && !compact_address);
            m_address_display[i]->SetLabel(m_address[i]->GetValue());
            m_address_display[i]->InvalidateBestSize();
            m_address_display[i]->SetToolTip(m_address[i]->GetValue());
            m_address_display[i]->Show(financial && compact_address);
            m_edit_address[i]->Show(financial && compact_address && funding && !paid && !view.sandbox);
            m_verify[i]->Show(financial && funding && !m_auto_failure[i].empty());
            // Keep focus, selection and caret intact while asynchronous validation runs.
            m_address[i]->Enable(financial && funding && !paid && !view.sandbox);
            m_address[i]->SetHint(human ? "Your Lightning address" : "CPU receiving wallet address");
            m_verify[i]->Enable(funding && !view.busy && !m_address_pending[i]);
            wxString verification;
            wxColour colour = MUTED;
            if (!financial) verification = "CPU • no payments";
            else if (m_address_pending[i]) verification = "Checking receiver...";
            else if (!m_auto_failure[i].empty()) { verification = m_auto_failure[i]; colour = ACCENT; }
            else if (status == 2) { verification = "Verified"; colour = GREEN; }
            else if (status == 3 || status == 5) { verification = wxString::FromUTF8(view.verification_message[i]); colour = ACCENT; }
            else if (m_address[i]->GetValue().empty() && !human && financial && !view.sandbox)
                verification = "Enter an address from a wallet you control. For automatic test wallets, use Set up CPU test wallets in the launcher.";
            else verification = m_address[i]->GetValue().empty() ? "Enter a Lightning address" : "Waiting for automatic check...";
            m_verification[i]->SetLabel(verification);
            m_verification[i]->Wrap(PLAYER_WIDTH);
            m_verification[i]->SetForegroundColour(colour);
            m_verification[i]->SetToolTip(status == 2 ? "Receiving address verified automatically; reused for up to 30 minutes. No funds sent for verification." : wxString::FromUTF8(view.verification_message[i]));
            const bool show = financial && !m_addresses_dirty[i] && status == 2 && !m_admitted && funding;
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
            m_pay[i]->SetToolTip(view.sandbox ? "Pay from this player's Sandbox test wallet" : "Simulate this player's entry payment");
            m_pay[i]->Show(financial && !paid && (view.simulated || view.sandbox) && show);
            m_pay[i]->Enable(show && view.phase == 1 && !view.busy && !paid);
            m_rows[i]->SetMinSize(wxSize(-1, m_rows[i]->GetSizer()->CalcMin().y));
            m_rows[i]->Layout();
        }
        for (int pair = 0; pair < 2; ++pair) {
            m_pairs[pair]->Show(m_rows[pair * 2]->IsShown() || m_rows[pair * 2 + 1]->IsShown());
            m_pairs[pair]->SetMinSize(wxSize(-1, m_pairs[pair]->GetSizer()->CalcMin().y));
            m_pairs[pair]->Layout();
        }

		m_error->SetLabel(wxString::FromUTF8(view.error[0] ? view.error : view.payment_error));
		m_error->SetToolTip(m_error->GetLabel());
		m_error->Show(view.error[0] != 0 || view.payment_error[0] != 0);
		wxString activity;
		if ((m_admitted || m_in_match) && (m_capture_failed || match.credit_stream_incomplete)) activity = "Incomplete match data • unspent funds will be returned\n";
		activity += wxString::FromUTF8(view.payment_log);
		if (activity.empty()) activity = "No payments yet.";
		if (m_log->GetValue() != activity) { m_log->ChangeValue(activity); m_log->ShowPosition(m_log->GetLastPosition()); }
        m_summary_available = view.summary_available != 0;
        const auto summary_id = wxString::FromUTF8(view.summary_match_id);
        if (m_summary_available && summary_id != m_summary_id) {
            m_summary_id = summary_id;
            BuildSummary(view);
            m_summary_auto = m_show_next_summary;
            m_show_next_summary = false;
            m_summary_start_request = css.start_requests;
            m_summary_manual = false;
        }
        // Some Melee builds skip results; keep the summary through CSS until Start.
        if (css.start_requests != m_summary_start_request) m_summary_manual = m_summary_auto = false;
        if (m_in_match || m_admitted) m_summary_manual = m_summary_auto = false;
        m_last_match->Enable(m_summary_available && !m_in_match && !m_admitted);
        LayoutSummary();
	}

	ArenaLocalHandle* m_handle = nullptr;
	wxTimer m_timer;
	bool m_enabled = true, m_start_pending = false, m_admitted = false;
	bool m_in_match = false, m_abandon = false, m_show_next_summary = false;
	std::uint64_t m_generation = 0, m_match_number = 0, m_log_revision = ~std::uint64_t(0);
	std::uint64_t m_lobby_generation = 0;
	bool m_capture_failed = false;
	bool m_credit_pending = false;
	wxStaticText* m_invoice_rule = nullptr;
	wxStaticText* m_entry_rule = nullptr;
	wxStaticText* m_rate_rule = nullptr;
	wxStaticText* m_wallet_mode = nullptr;
	wxStaticText* m_phase = nullptr;
	wxStaticText* m_background_payments = nullptr;
	wxStaticText* m_error = nullptr;
	wxPanel* m_rows[4] = {};
	wxPanel* m_pairs[2] = {};
    wxPanel* m_summary = nullptr;
    wxPanel* m_summary_grid = nullptr;
    wxStaticText* m_summary_title = nullptr;
    wxStaticText* m_summary_note = nullptr;
    wxToggleButton* m_last_match = nullptr;
    wxString m_summary_id;
    bool m_summary_available = false, m_summary_auto = false, m_summary_manual = false;
    std::uint64_t m_summary_start_request = 0;
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
	wxStaticBitmap* m_qr[4] = {};
	wxTextCtrl* m_address[4] = {};
	wxStaticText* m_phone_room = nullptr;
	wxString m_phone_room_status;
	wxStaticText* m_address_display[4] = {};
	wxHyperlinkCtrl* m_edit_address[4] = {};
	bool m_editing_address[4] = {};
	wxButton* m_verify[4] = {};
	wxStaticText* m_address_warning = nullptr;
	bool m_addresses_loaded = false, m_addresses_dirty[4] = {};
	bool m_address_pending[4] = {};
	wxString m_address_submitted[4];
	wxString m_auto_attempted[4], m_auto_failure[4];
	std::chrono::steady_clock::time_point m_edited_at[4] = {}, m_auto_started[4] = {};
	wxTextCtrl* m_log = nullptr;
	wxCheckBox* m_show_activity = nullptr;
	wxFrame* m_log_window = nullptr;
	wxPanel* m_logs = nullptr;
	wxPanel* m_log_area = nullptr;
	wxCheckBox* m_show_outbound = nullptr;
	wxPanel* m_outbound_area = nullptr;
	wxTextCtrl* m_outbound = nullptr;
	std::vector<char> m_outbound_buffer = std::vector<char>(4096);
	std::chrono::steady_clock::time_point m_outbound_refreshed = {};
	wxButton* m_pay[4] = {};
	wxButton* m_discard = nullptr;
	wxButton* m_pay_missed = nullptr;
    wxCheckBox* m_show_missed = nullptr;
    wxPanel* m_missed_area = nullptr;
    wxTextCtrl* m_missed = nullptr;
    std::uint64_t m_payable_missed_sats = 0;
	std::uint64_t m_unsent_sats = 0;
	wxString m_last_invoice[4];
};
}
wxWindow* CreateSidebar(wxWindow* parent) { return new FundingPanel(parent); }
}
