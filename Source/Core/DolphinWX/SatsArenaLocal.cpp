// Copyright 2026 Sats Arena contributors. Licensed under GPLv2+.
#include "DolphinWX/SatsArenaLocal.h"
#include "DolphinWX/SatsArenaLocalLayout.h"

#include <algorithm>
#include <functional>
#include <array>
#include <cstdlib>
#include <chrono>
#include <cmath>
#include <vector>
#include <wx/panel.h>
#include <cstring>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dcbuffer.h>
#include <wx/dcmemory.h>
#include <wx/frame.h>
#include <wx/hyperlink.h>
#include <wx/msgdlg.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/tglbtn.h>

#include "Common/FileUtil.h"
#include "Core/Core.h"
#include "Core/Slippi/SatsArenaTracker.h"
#include "Core/Slippi/SatsArenaNetplayRoom.h"
#include "Core/Slippi/SatsArenaRoomClient.h"
#include "DolphinWX/SatsArenaRoomView.h"
#include "DolphinWX/SatsArenaWidgets.h"
#include "sats_arena_local.h"
static_assert(sizeof(ArenaLocalView) == 19144, "Arena local ABI layout mismatch");

namespace SatsArenaLocal
{
namespace
{
// Bundled wxWidgets needs default-constructible event functors on MSVC.

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

const wxColour BACKGROUND(14, 20, 30);
const wxColour SURFACE(27, 36, 51);
const wxColour TEXT_COLOUR(241, 243, 247);
const wxColour MUTED(161, 173, 191);
const wxColour BORDER(77, 91, 113);
const wxColour ACCENT(248, 115, 102);
const wxColour GOLD(255, 204, 72);
const wxColour GREEN(143, 209, 166);
const std::array<wxColour, 4> PLAYER_COLOURS = {{ACCENT, wxColour(117, 167, 248), GOLD, wxColour(117, 216, 139)}};
const std::array<wxColour, 3> TEAM_COLOURS = {{PLAYER_COLOURS[0], PLAYER_COLOURS[1], PLAYER_COLOURS[3]}};
const int CONTENT_WIDTH = 460;
const int PLAYER_WIDTH = 224;
const int MARGIN = 6;

wxStaticText* Label(wxWindow* parent, const wxString& text, int points,
                    const wxColour& colour = TEXT_COLOUR, bool bold = false, long style = 0)
{
    auto* label = new wxStaticText(parent, wxID_ANY, text, wxDefaultPosition, wxDefaultSize, style);
    label->SetForegroundColour(colour);
    label->SetFont(wxFont(points, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                         bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL));
    return label;
}
wxStaticText* Heading(wxWindow* parent, const wxString& text, int points,
                      const wxColour& colour = TEXT_COLOUR, long style = 0)
{
    auto* label = Label(parent, text, points, colour, true, style);
    label->SetFont(wxFont(points, wxFONTFAMILY_SWISS, wxFONTSTYLE_SLANT,
                         wxFONTWEIGHT_HEAVY, false, "DejaVu Sans Condensed"));
    return label;
}
wxPanel* Divider(wxWindow* parent)
{
    auto* line = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    line->SetBackgroundColour(BORDER);
    line->SetMinSize(wxSize(-1, 1));
    return line;
}
using SatsArenaWidgets::InvoiceQR;

class FundingPanel final : public wxScrolledWindow
{
public:
    explicit FundingPanel(wxWindow* parent, ViewCallback view)
        : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL),
          m_timer(this), m_view_callback(std::move(view))
    {
        SetScrollRate(0, 12);
        SetBackgroundColour(BACKGROUND);
        SetForegroundColour(TEXT_COLOUR);
        SetName("Super Sats Bro Melee match funding");
#if wxUSE_PRIVATE_FONTS
        wxFont::AddPrivateFont(wxString::FromUTF8((File::GetSysDirectory() + "SatsArena/DejaVuSansCondensed-BoldOblique.ttf").c_str()));
#endif
        const char* env = std::getenv("SATS_ARENA_FAKE_LOCAL");
        m_remote = SatsArenaNetplay::ServerEnabled();
        m_enabled = !SatsArenaNetplay::Enabled() && (!env || std::strcmp(env, "0") != 0);
        SatsArena::GetPaymentGate().Enable(m_enabled);
        m_handle = m_enabled ? GetHost().Get() : nullptr;
        auto* root = new wxBoxSizer(wxVERTICAL);
        auto* header = new wxBoxSizer(wxHORIZONTAL);
        header->Add(Heading(this, "SATS MELEE", 14, GOLD), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, MARGIN);
        m_phase = Label(this, "Connecting wallet...", 10, MUTED);
        header->Add(m_phase, 1, wxALIGN_CENTER_VERTICAL);
        m_wallet_mode = Label(this, "", 8, ACCENT);
        header->Add(m_wallet_mode, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 6);
        m_wallet_mode->Hide();
        root->Add(header, 0, wxALL | wxEXPAND, MARGIN);
        if (m_remote) {
            m_room_options = new wxPanel(this);
            auto* options = new wxBoxSizer(wxVERTICAL);
            options->Add(Heading(m_room_options, "CREATE ROOM OPTIONS", 9, MUTED), 0, wxBOTTOM, 4);
            auto* settings = new wxBoxSizer(wxHORIZONTAL);
            settings->Add(Label(m_room_options, "Players", 9), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
            m_room_count = new wxChoice(m_room_options, wxID_ANY);
            m_room_count->Append("2"); m_room_count->Append("3"); m_room_count->Append("4");
            m_room_count->SetName("Room player count");
            settings->Add(m_room_count, 0, wxRIGHT, 12);
            m_room_mode = new wxChoice(m_room_options, wxID_ANY);
            m_room_mode->Append("Free-for-all"); m_room_mode->Append("Teams");
            m_room_mode->SetName("Room mode");
            settings->Add(m_room_mode, 1);
            options->Add(settings, 0, wxEXPAND | wxBOTTOM, 4);
            auto* teams = new wxBoxSizer(wxHORIZONTAL);
            for (int i = 0; i < 4; ++i) {
                auto* box = new wxBoxSizer(wxVERTICAL);
                m_room_team_labels[i] = Label(m_room_options, wxString::Format("PLAYER %d", i + 1), 8, PLAYER_COLOURS[i]);
                box->Add(m_room_team_labels[i], 0);
                m_room_teams[i] = new wxChoice(m_room_options, wxID_ANY);
                m_room_teams[i]->Append("Red"); m_room_teams[i]->Append("Blue"); m_room_teams[i]->Append("Green");
                m_room_teams[i]->SetName(wxString::Format("Player %d room team", i + 1));
                m_room_teams[i]->SetSelection(i % 2);
                box->Add(m_room_teams[i], 0, wxEXPAND);
                teams->Add(box, 1, i < 3 ? wxRIGHT : 0, 6);
                m_room_teams[i]->Bind(wxEVT_CHOICE, std::function<void(wxCommandEvent&)>([this](wxCommandEvent&) { SaveRoomConfig(); }));
            }
            options->Add(teams, 0, wxEXPAND);
            m_room_config_note = Label(m_room_options, "Choose before creating; joining uses the host's rules.", 8, MUTED);
            options->Add(m_room_config_note, 0, wxTOP | wxEXPAND, 4);
            m_room_options->SetSizer(options);
            root->Add(m_room_options, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, MARGIN);
            m_room_count->Bind(wxEVT_CHOICE, std::function<void(wxCommandEvent&)>([this](wxCommandEvent&) { SaveRoomConfig(); }));
            m_room_mode->Bind(wxEVT_CHOICE, std::function<void(wxCommandEvent&)>([this](wxCommandEvent&) { SaveRoomConfig(); }));
            ShowRoomConfig(SatsArenaRoomClient::CreateConfig());
        }
        m_phone_join = new wxPanel(this);
        m_phone_join->SetBackgroundColour(SURFACE);
        m_phone_join->SetBackgroundStyle(wxBG_STYLE_PAINT);
        m_phone_join->Bind(wxEVT_PAINT, std::function<void(wxPaintEvent&)>([this](wxPaintEvent&) {
            wxAutoBufferedPaintDC dc(m_phone_join);
            dc.SetBackground(wxBrush(SURFACE));
            dc.Clear();
            const auto size = m_phone_join->GetClientSize();
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.SetPen(wxPen(BORDER));
            dc.DrawRectangle(0, 0, size.x, size.y);
            dc.SetPen(wxPen(GOLD, 2));
            dc.DrawLine(0, 0, size.x - 14, 0);
            dc.DrawLine(size.x - 14, 0, size.x - 1, 13);
            dc.SetPen(wxPen(ACCENT, 2));
            dc.DrawLine(0, size.y - 1, size.x / 3, size.y - 1);
        }));
        auto* phone_layout = new wxBoxSizer(wxHORIZONTAL);
        auto* website = new wxBoxSizer(wxVERTICAL);
        website->Add(Heading(m_phone_join, "JOIN ON YOUR PHONE", 9, MUTED), 0, wxBOTTOM, 3);
        m_phone_website = Label(m_phone_join, "", 16, TEXT_COLOUR, true);
        m_phone_website->SetMinSize(wxSize(240, -1));
        website->Add(m_phone_website, 0, wxEXPAND);
        phone_layout->Add(website, 1, wxALL | wxALIGN_CENTER_VERTICAL, 8);
        auto* room = new wxBoxSizer(wxVERTICAL);
        room->Add(Heading(m_phone_join, "ROOM CODE", 9, GOLD), 0, wxBOTTOM, 3);
        m_phone_code = Label(m_phone_join, "", 26, GOLD, true);
        m_phone_code->SetFont(wxFont(26, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        room->Add(m_phone_code, 0);
        phone_layout->Add(room, 0, wxALL | wxALIGN_CENTER_VERTICAL, 8);
        m_phone_join->SetSizer(phone_layout);
        root->Add(m_phone_join, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, MARGIN);
        m_phone_join->Hide();
        m_phone_room = Label(this, "", 9, ACCENT);
        m_phone_room->SetName("Phone room connection");
        root->Add(m_phone_room, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, MARGIN);
        m_phone_room->Hide();
        m_background_payments = Label(this, "", 9, MUTED);
        root->Add(m_background_payments, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, MARGIN);
        m_background_payments->Hide();

        auto* rules = new wxBoxSizer(wxHORIZONTAL);
        auto* entry_rule = new wxBoxSizer(wxVERTICAL);
        entry_rule->Add(Heading(this, "ENTRY INVOICE / PLAYER", 8, MUTED), 0, wxBOTTOM, 2);
        m_invoice_rule = Heading(this, "...", 20, GOLD);
        m_invoice_rule->SetToolTip("The routing contribution is added to the invoice and retained by the host wallet for fees, including after cancellation. It is separate from the playable pot.");
        entry_rule->Add(m_invoice_rule, 0, wxEXPAND);
        m_entry_rule = Label(this, "", 9, MUTED);
        entry_rule->Add(m_entry_rule, 0, wxEXPAND);
        rules->Add(entry_rule, 1, wxRIGHT, 10);
        auto* rate_rule = new wxBoxSizer(wxVERTICAL);
        rate_rule->Add(Heading(this, "DAMAGE RATE", 8, MUTED), 0, wxBOTTOM, 2);
        m_rate_rule = Heading(this, "...", 14, TEXT_COLOUR, wxST_ELLIPSIZE_END);
        m_rate_rule->SetMinSize(wxSize(100, -1));
        rate_rule->Add(m_rate_rule, 0, wxEXPAND);
        rules->Add(rate_rule, 1);
        root->Add(rules, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, MARGIN);
        m_address_warning = Label(this, "", 9, ACCENT);
        root->Add(m_address_warning, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, MARGIN);
        m_address_warning->Hide();
        root->Add(Divider(this), 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, MARGIN);

        m_summary = new wxPanel(this);
        auto* summary_layout = new wxBoxSizer(wxVERTICAL);
        m_summary_title = Heading(m_summary, "Last match", 14, GOLD);
        summary_layout->Add(m_summary_title, 0, wxBOTTOM | wxEXPAND, 12);
        summary_layout->Add(Label(m_summary, "Damage dealt (%)", 10, TEXT_COLOUR, true), 0, wxBOTTOM | wxEXPAND, 12);
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
            row->SetForegroundColour(TEXT_COLOUR);
            auto* info = new wxBoxSizer(wxVERTICAL);
            auto* heading = new wxBoxSizer(wxHORIZONTAL);
            m_player[i] = Heading(row, wxString::Format("PLAYER %d • Human", i + 1), 10, PLAYER_COLOURS[i], wxST_ELLIPSIZE_END);
            m_player[i]->SetMinSize(wxSize(80, -1));
            heading->Add(m_player[i], 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
            m_entry_status[i] = Heading(row, "UNPAID", 9, ACCENT);
            heading->Add(m_entry_status[i], 0, wxALIGN_CENTER_VERTICAL);
            info->Add(heading, 0, wxEXPAND | wxBOTTOM, 3);

            m_address[i] = new wxTextCtrl(row, wxID_ANY);
            m_address[i]->SetHint("name@wallet.com");
            m_address[i]->SetMaxLength(127);
            m_address[i]->SetMinSize(wxSize(80, -1));
            m_address[i]->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
            m_address[i]->SetBackgroundColour(wxColour(36, 38, 46));
            m_address[i]->SetForegroundColour(TEXT_COLOUR);
            m_address[i]->SetName(wxString::Format("Player %d Lightning Address", i + 1));
            m_address[i]->Bind(wxEVT_TEXT, std::function<void(wxCommandEvent&)>([this, i](wxCommandEvent&) {
                m_addresses_dirty[i] = true;
                m_edited_at[i] = std::chrono::steady_clock::now();
                m_auto_failure[i].clear();
                m_auto_attempted[i].clear();
            }));
            m_address[i]->Bind(wxEVT_KILL_FOCUS, std::function<void(wxFocusEvent&)>([this, i](wxFocusEvent& event) {
                m_editing_address[i] = false;
                event.Skip();
            }));
            m_address[i]->Bind(wxEVT_SET_FOCUS, std::function<void(wxFocusEvent&)>([this, i](wxFocusEvent& event) {
                m_editing_address[i] = true;
                event.Skip();
            }));
            m_address[i]->Bind(wxEVT_CHAR_HOOK, std::function<void(wxKeyEvent&)>([this, i](wxKeyEvent& event) {
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
            }));
            auto* address_row = new wxBoxSizer(wxHORIZONTAL);
            address_row->Add(m_address[i], 1, wxRIGHT | wxALIGN_CENTER_VERTICAL, 6);
            m_address_display[i] = Label(row, "", 8, MUTED, false, wxST_ELLIPSIZE_END);
            m_address_display[i]->SetMinSize(wxSize(80, -1));
            address_row->Add(m_address_display[i], 1, wxRIGHT | wxALIGN_CENTER_VERTICAL, 6);
            m_edit_address[i] = new wxHyperlinkCtrl(row, wxID_ANY, "Edit", "");
            m_edit_address[i]->SetName(wxString::Format("Edit Player %d receiving address", i + 1));
            m_edit_address[i]->SetNormalColour(MUTED);
            m_edit_address[i]->SetVisitedColour(MUTED);
            m_edit_address[i]->SetHoverColour(TEXT_COLOUR);
            m_edit_address[i]->Bind(wxEVT_HYPERLINK, std::function<void(wxHyperlinkEvent&)>([this, i](wxHyperlinkEvent&) {
                EditAddress(i);
            }));
            address_row->Add(m_edit_address[i], 0, wxALIGN_CENTER_VERTICAL);
            m_verify[i] = new wxButton(row, wxID_ANY, "Retry", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
            m_verify[i]->SetName(wxString::Format("Retry Player %d receiving address", i + 1));
            m_verify[i]->Bind(wxEVT_BUTTON, std::function<void(wxCommandEvent&)>([this, i](wxCommandEvent&) {
                m_auto_attempted[i].clear();
                m_auto_failure[i].clear();
            }));
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
                auto* value = Heading(row, "0 sats", 10, colour, wxST_ELLIPSIZE_END);
                value->SetMinSize(wxSize(68, -1));
                column->Add(value, 0, wxEXPAND);
                m_metrics[i]->Add(column, 1, wxEXPAND);
                return value;
            };
            m_pot[i] = metric("POT", TEXT_COLOUR);
            m_paid_out[i] = metric("PAID OUT", TEXT_COLOUR);
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
            m_pay[i]->Bind(wxEVT_BUTTON, std::function<void(wxCommandEvent&)>([this, i](wxCommandEvent&) { Send(i < 2 ? 2 + i : 7 + i); }));
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
        m_show_activity->SetForegroundColour(TEXT_COLOUR);
        log_toggles->Add(m_show_activity, 0, wxRIGHT, MARGIN);
        m_show_outbound = new wxCheckBox(this, wxID_ANY, "Outbound payments");
        m_show_outbound->SetForegroundColour(TEXT_COLOUR);
        m_show_outbound->SetToolTip("Debug all queued, in-flight, paid and failed payments, including earlier games. Select text to pause updates for copying.");
        log_toggles->Add(m_show_outbound, 0, wxRIGHT, 8);
        m_show_missed = new wxCheckBox(this, wxID_ANY, "Missed");
        m_show_missed->SetForegroundColour(TEXT_COLOUR);
        m_show_missed->SetToolTip("Amounts still owed, automatic settlement status, and payments needing Retry now.");
        log_toggles->Add(m_show_missed, 0);
        m_last_match = new wxToggleButton(this, wxID_ANY, "Last match", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        m_last_match->SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        m_last_match->Enable(false);
        log_toggles->AddStretchSpacer();
        log_toggles->Add(m_last_match, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 6);
        m_last_match->Bind(wxEVT_TOGGLEBUTTON, std::function<void(wxCommandEvent&)>([this](wxCommandEvent&) {
            m_summary_manual = m_last_match->GetValue();
            m_summary_auto = false;
            m_summary_start_request = SatsArena::GetPaymentGate().Read().start_requests;
            LayoutSummary();
        }));
        root->Add(log_toggles, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, MARGIN);
        m_discard = new wxButton(this, wxID_ANY, "Discard unsent payments");
        m_discard->Enable(false);
        m_discard->SetToolTip("Discard queued payouts and open damage batches from this and earlier matches. Funds stay in the host wallet. Submitted Lightning payments cannot be cancelled.");
        m_discard->Bind(wxEVT_BUTTON, std::function<void(wxCommandEvent&)>([this](wxCommandEvent&) {
            wxMessageDialog confirm(this,
                wxString::Format("Discard up to %llu sats in unsent payments?\n\nThese funds will stay in the host wallet and will not be paid to players. Already-submitted payments may still complete. Payment history is kept.",
                    static_cast<unsigned long long>(m_unsent_sats)),
                "Discard unsent payments", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
            confirm.SetYesNoLabels("Discard unsent", "Keep payments");
            if (confirm.ShowModal() == wxID_YES) Send(14);
        }));
        m_pay_missed = new wxButton(this, wxID_ANY, "Retry now");
        m_pay_missed->Enable(false);
        m_pay_missed->SetToolTip("Resume settlement after automatic retries pause. Unknown original sends keep their original hash.");
        m_pay_missed->Bind(wxEVT_BUTTON, std::function<void(wxCommandEvent&)>([this](wxCommandEvent&) {
            wxMessageDialog confirm(this,
                wxString::Format("Retry settlement of %llu sats?\n\nResume automatic settlement for safe amounts from finished matches. Unconfirmed original sends are excluded. Routing fees follow your wallet policy.",
                    static_cast<unsigned long long>(m_payable_missed_sats)),
                "Retry settlement", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
            confirm.SetYesNoLabels("Retry payouts", "Cancel");
            if (confirm.ShowModal() == wxID_YES) Send(16);
        }));
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
        m_outbound->SetForegroundColour(TEXT_COLOUR);
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
        m_missed->SetForegroundColour(TEXT_COLOUR);
        missed_layout->Add(m_missed, 1, wxALL | wxEXPAND, 8);
        m_missed_area->SetSizer(missed_layout);
        logs_layout->Add(m_missed_area, 1, wxEXPAND);
        m_missed_area->Hide();
        m_logs->SetSizer(logs_layout);
        auto* log_window_layout = new wxBoxSizer(wxVERTICAL);
        log_window_layout->Add(m_logs, 1, wxALL | wxEXPAND, MARGIN);
        m_log_window->SetSizer(log_window_layout);
        m_log_window->Bind(wxEVT_CLOSE_WINDOW, std::function<void(wxCloseEvent&)>([this](wxCloseEvent& event) {
            if (!event.CanVeto()) { event.Skip(); return; }
            m_show_activity->SetValue(false);
            m_show_outbound->SetValue(false);
            m_show_missed->SetValue(false);
            m_missed_area->Hide();
            if (m_handle) arena_local_outbound_debug(m_handle, false);
            m_log_area->Hide();
            m_outbound_area->Hide();
            LayoutContents();
            event.Veto();
        }));
        m_show_activity->Bind(wxEVT_CHECKBOX, std::function<void(wxCommandEvent&)>([this](wxCommandEvent&) {
            m_log_area->Show(m_show_activity->GetValue());
            LayoutContents();
        }));
        m_show_outbound->Bind(wxEVT_CHECKBOX, std::function<void(wxCommandEvent&)>([this](wxCommandEvent&) {
            const bool show = m_show_outbound->GetValue();
            if (m_handle) arena_local_outbound_debug(m_handle, show);
            m_outbound_area->Show(show);
            m_outbound_refreshed = {};
            if (show) UpdateOutbound();
            LayoutContents();
        }));
        m_show_missed->Bind(wxEVT_CHECKBOX, std::function<void(wxCommandEvent&)>([this](wxCommandEvent&) {
            m_missed_area->Show(m_show_missed->GetValue());
            LayoutContents();
        }));
        if (m_remote) {
            m_discard->Hide();
            m_pay_missed->Hide();
            m_show_outbound->Hide();
            m_show_missed->Hide();
        }
        SetSizer(root);
        Bind(wxEVT_CHAR_HOOK, std::function<void(wxKeyEvent&)>([this](wxKeyEvent& event) {
            if (event.GetKeyCode() == WXK_ESCAPE && m_summary->IsShown()) {
                m_summary_manual = m_summary_auto = false;
                LayoutSummary();
            } else event.Skip();
        }));
        Bind(wxEVT_TIMER, std::function<void(wxTimerEvent&)>([this](wxTimerEvent&) { Poll(); }));
        m_timer.Start(100);
        Poll();
        m_refresh_header = true;
    }

	~FundingPanel() override
	{
		m_timer.Stop();
		if (m_handle) arena_local_outbound_debug(m_handle, false);
		m_log_window->Destroy();
	}

    void NextSetup()
    {
        if (!m_admitted && !m_in_match) m_next_setup_requested = true;
    }

    void FocusPlayer(int player)
    {
        m_summary_manual = player < 0 && m_summary_available && !m_admitted && !m_in_match;
        m_summary_auto = false;
        LayoutSummary();
        if (player >= 0 && player < 4 && m_rows[player]->IsShown())
        {
            const auto point = CalcUnscrolledPosition(ScreenToClient(m_rows[player]->ClientToScreen(wxPoint(0, 0))));
            Scroll(0, std::max(0, point.y / 12));
            m_rows[player]->SetFocus();
        }
    }

private:
    void PublishView(const ArenaLocalView& view)
    {
        if (!m_view_callback) return;
        const bool attention = view.phase == 7 || view.error[0] || view.payment_error[0] ||
            view.background_failed_sats || view.payable_missed_sats || m_capture_failed;
        m_view_callback(view, m_strip, m_phase->GetLabel(), attention);
    }

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
            grid->Add(Label(m_summary_grid, wxString::Format("P%d", p + 1), 10, TEXT_COLOUR, true), 0, wxALIGN_CENTER);
        }
        for (int attacker : players) {
            grid->Add(Label(m_summary_grid, wxString::Format(view.summary_kinds[attacker] == 1 ? "P%d CPU" : "P%d", attacker + 1), 9, TEXT_COLOUR, true), 0, wxALIGN_CENTER_VERTICAL);
            for (int victim : players) {
                const auto centi = view.summary_damage_centi[attacker][victim];
                const wxString value = attacker == victim ? wxString("—") :
                    (!view.summary_complete && centi == 0 ? wxString("?") : wxString::Format("%llu.%02llu%%",
                        static_cast<unsigned long long>(centi / 100), static_cast<unsigned long long>(centi % 100)));
                auto* cell = Label(m_summary_grid, value, 10, attacker == victim ? MUTED : TEXT_COLOUR);
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
		FitInside();
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
    void ResetRemote()
    {
        for (int i = 0; i < 4; ++i) {
            m_address[i]->ChangeValue("");
            m_addresses_dirty[i] = m_address_pending[i] = m_editing_address[i] = false;
            m_address_submitted[i].clear();
            m_auto_attempted[i].clear();
            m_auto_failure[i].clear();
        }
        m_summary_id.clear();
        m_summary_manual = m_summary_auto = m_summary_available = false;
    }

    void ShowRoomConfig(const SatsArenaNetplay::Config& config)
    {
        const auto value = SatsArenaNetplay::ConfigJson(config);
        if (value == m_room_display_config) return;
        m_room_display_config = value;
        m_room_count->SetSelection(config.player_count - 2);
        m_room_mode->SetSelection(config.teams ? 1 : 0);
        for (unsigned i = 0; i < 4; ++i) {
            const bool show = config.teams && i < config.player_count;
            m_room_teams[i]->Show(show);
            m_room_team_labels[i]->Show(show);
            if (show) m_room_teams[i]->SetSelection(config.team_ids[i]);
        }
        m_room_options->Layout();
        Layout();
        FitInside();
    }

    void SaveRoomConfig()
    {
        SatsArenaNetplay::Config config;
        config.player_count = m_room_count->GetSelection() + 2;
        config.teams = m_room_mode->GetSelection() == 1;
        for (unsigned i = 0; i < config.player_count; ++i)
            if (config.teams) config.team_ids[i] = m_room_teams[i]->GetSelection();
        const bool saved = SatsArenaRoomClient::SetCreateConfig(config);
        m_room_config_note->SetLabel(saved ? "Choose before creating; joining uses the host's rules." :
            "Teams need at least two colors. Options are fixed while in a room.");
        m_room_display_config = nullptr;
        ShowRoomConfig(saved ? config : SatsArenaRoomClient::CreateConfig());
    }

    void PollRemote()
    {
        const auto client = SatsArenaRoomClient::Active();
        m_room_options->Enable(!client);
        if (m_room_options->Show(!client)) { Layout(); FitInside(); }
        ShowRoomConfig(client ? client->GetConfig() : SatsArenaRoomClient::CreateConfig());
        if (client != m_remote_client.lock() || (!client && m_remote_slot != -1)) {
            ResetRemote();
            m_remote_client = client;
        }
        m_remote_slot = client ? client->LocalSlot() : -1;
        if (m_remote_slot < 0 || m_remote_slot > 3) m_remote_slot = -1;
        m_remote_notice.clear();
        ArenaLocalView view = {};
        std::fill(std::begin(view.kinds), std::end(view.kinds), 3);
        SatsArenaNetplay::Room room;
        std::string error;
        if (client && client->Poll(room, error) == SatsArenaRoomClient::Status::Failed) {
            ResetRemote();
            m_phone_room->Hide();
            view.phase = 7;
            const auto message = error.empty() ? std::string("Room connection failed") : error;
            std::memcpy(view.error, message.data(), std::min(message.size(), sizeof(view.error) - 1));
            RenderView(view, false, false, true);
            return;
        }
        const auto code = client ? client->GetRoomCode() : std::string();
        const auto config = client ? client->GetConfig() : SatsArenaNetplay::Config();
        m_phone_room->SetLabel(code.empty() ? wxString() : wxString::Format("Room %s • %u players • %s • You: P%d",
            wxString::FromUTF8(code.c_str()), config.player_count, config.teams ? "Teams" : "FFA", m_remote_slot + 1));
        m_phone_room->Show(!code.empty() && m_remote_slot >= 0);
        const auto payment = client ? client->PaymentView() : nlohmann::json(nullptr);
        if (payment.is_null()) {
            ResetRemote();
            m_remote_notice = !client ? "Create or join a room in Melee" :
                (m_remote_slot < 0 ? "Connecting to room..." : "Free room • no entry payment");
            RenderView(view, false, false, true);
            return;
        }
        if (!RoomView(payment, view) || m_remote_slot == -1) {
            ResetRemote();
            view = {};
            view.phase = 7;
            std::fill(std::begin(view.kinds), std::end(view.kinds), 3);
            std::strcpy(view.error, "Invalid room payment state; no invoice can be displayed.");
            RenderView(view, false, false, true);
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        for (int i = 0; i < 4; ++i) {
            const auto address = wxString::FromUTF8(view.addresses[i]);
            if (i != m_remote_slot || (!m_addresses_dirty[i] && !m_address_pending[i]))
                if (m_address[i]->GetValue() != address) m_address[i]->ChangeValue(address);
            if (i != m_remote_slot) continue;
            if (m_address_pending[i]) {
                if (m_address[i]->GetValue() != m_address_submitted[i]) {
                    m_address_pending[i] = false;
                    m_auto_attempted[i].clear();
                } else if (address == m_address_submitted[i]) {
                    m_addresses_dirty[i] = false;
                    // Leave time for the queued command to reach the server before
                    // interpreting an earlier failed check of the same address.
                    if (view.verification_state[i] != 0 && view.verification_state[i] != 1 &&
                        now - m_auto_started[i] >= std::chrono::seconds(1))
                        m_address_pending[i] = false;
                }
                if (m_address_pending[i] &&
                    ((view.error[0] && wxString::FromUTF8(view.error) != m_remote_error_at_submit) ||
                     now - m_auto_started[i] > std::chrono::seconds(45))) {
                    m_address_pending[i] = false;
                    m_auto_failure[i] = view.error[0] ? wxString::FromUTF8(view.error) :
                        wxString("Server did not confirm the address. Retry when connected.");
                }
            }
            if (!m_addresses_dirty[i] && !m_address_pending[i]) {
                if (view.verification_state[i] == 2) m_auto_failure[i].clear();
                else if (view.verification_state[i] == 3 || view.verification_state[i] == 5)
                    m_auto_failure[i] = wxString::FromUTF8(view.verification_message[i]);
            }
            const auto input = m_address[i]->GetValue();
            const bool funding = view.phase == 1 || view.phase == 2;
            if (funding && !view.paid_sats[i] && !m_address_pending[i] && !input.empty() &&
                (m_addresses_dirty[i] || view.verification_state[i] != 2) && input != m_auto_attempted[i] &&
                now - m_edited_at[i] >= std::chrono::milliseconds(750)) {
                m_auto_attempted[i] = input;
                if (client->SetAddress(input.utf8_str().data())) {
                    m_address_pending[i] = true;
                    m_address_submitted[i] = input;
                    m_auto_started[i] = now;
                    m_auto_failure[i].clear();
                    m_remote_error_at_submit = wxString::FromUTF8(view.error);
                } else m_auto_failure[i] = "Address could not be queued; check the room connection.";
            }
        }
        RenderView(view, m_address_pending[m_remote_slot],
            !m_addresses_dirty[m_remote_slot] && view.verification_state[m_remote_slot] == 2, true);
    }
	void Poll()
	{
		if (m_remote) { PollRemote(); return; }
		if (!m_enabled) { m_phase->SetLabel("Payment gate disabled"); return; }
		ArenaLocalView view = {};
		if (!m_handle || !arena_local_snapshot(m_handle, &view)) return;
		if (view.abi_version != 13) { m_error->SetLabel("Payment engine ABI mismatch"); return; }
        if (m_refresh_header) {
            for (auto* child : GetChildren())
                if (child->IsKindOf(CLASSINFO(wxStaticText))) child->InvalidateBestSize();
            for (auto* child : m_phone_join->GetChildren()) child->InvalidateBestSize();
            m_phone_join->InvalidateBestSize();
            m_phone_join->Layout();
            Layout();
            m_refresh_header = false;
        }
        char room_status[512] = {};
        if (arena_local_room_status(m_handle, room_status, sizeof(room_status))) {
            const auto status = wxString::FromUTF8(room_status);
            if (m_phone_room_status != status) {
                m_phone_room_status = status;
                const wxString prefix = "Phone room ";
                const auto separator = status.Find(" • ");
                const bool has_room = status.StartsWith(prefix) && separator >= static_cast<int>(prefix.length());
                const auto code = has_room ? status.Mid(prefix.length(), separator - prefix.length()) : wxString();
                const auto website = has_room ? status.Mid(separator + 3) : wxString();
                const bool joined = status.StartsWith(prefix) && code.length() == 6 &&
                    (website.StartsWith("http://") || website.StartsWith("https://"));
                m_phone_join->Show(joined);
                if (joined) {
                    m_phone_code->SetLabel(code);
                    m_phone_website->SetLabel(website.Mid(website.StartsWith("https://") ? 8 : 7));
                    m_phone_website->SetToolTip(website);
                    m_phone_website->Wrap(CONTENT_WIDTH - 160);
                }
                m_phone_room->SetLabel(status);
                m_phone_room->Wrap(CONTENT_WIDTH);
                m_phone_room->Show(!joined && !status.empty());
                m_refresh_header = true;
                Layout();
            }
        }
		m_unsent_sats = view.unsent_sats;
		m_discard->Enable(view.unsent_sats != 0 && !view.busy);
        m_payable_missed_sats = view.payable_missed_sats;
        m_pay_missed->Enable(view.payable_missed_sats > 0 && !view.busy);
        m_show_missed->SetForegroundColour(view.missed_sats ? ACCENT : TEXT_COLOUR);
        m_show_missed->SetToolTip(wxString::Format("%llu sats awaiting settlement; %llu sats need Retry now. Open for amounts, addresses, and status.",
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
        const bool strip = UseBottomStrip(m_strip, m_in_match,
            funding && css.roster_valid && !m_admitted && !m_in_match);
        if (strip != m_strip)
        {
            m_strip = strip;
            if (!strip)
            {
                m_summary_manual = m_summary_auto = false;
                m_next_setup_requested = false;
            }
            PublishView(view);
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
                RenderView(view, checking_addresses, addresses_saved, roster_matches);
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
				if (Send(result))
                {
                    m_in_match = false; m_admitted = false; m_show_next_summary = true;
                    m_result_start_request = css.start_requests;
                    m_next_setup_requested = false;
                }
			}
			else if (m_abandon || (!m_admitted && !m_start_pending) ||
				(m_admitted && !m_in_match && (!Core::IsRunning() || css.generation != m_generation)))
			{
				if (Send(8)) { m_abandon = false; m_admitted = false; }
			}
		}
		else if (!view.busy && !m_admitted && !m_start_pending)
		{
			if ((view.phase == 4 || view.phase == 6) && css.roster_valid &&
                WantsNextSetup(m_strip, m_next_setup_requested, css.start_requests, m_result_start_request))
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
        RenderView(view, checking_addresses, addresses_saved, roster_matches);
    }

    void RenderView(const ArenaLocalView& view, bool checking_addresses, bool addresses_saved, bool roster_matches)
    {
        if (m_remote && m_refresh_header) {
            for (auto* child : GetChildren())
                if (child->IsKindOf(CLASSINFO(wxStaticText))) child->InvalidateBestSize();
            m_refresh_header = false;
        }
        const auto css = SatsArena::GetPaymentGate().Read();
        const auto match = SatsArena::GetTracker().Read();
        const bool funding = view.phase == 1 || view.phase == 2;
        const bool show_css = !m_remote && !m_admitted && !m_in_match && (css.kinds[0] < 2 || css.kinds[1] < 2 || css.kinds[2] < 2 || css.kinds[3] < 2);
        wxString phase;
        if (m_remote) {
            if (!m_remote_notice.empty()) phase = m_remote_notice;
            else if (view.phase == 7 || view.error[0]) phase = "Payment needs attention";
            else if (view.phase == 6) phase = "Server is settling payments...";
            else if (view.phase == 4) phase = "Match settled";
            else if (view.phase == 3) phase = "Match in progress";
            else if (view.phase == 2) phase = "Entries confirmed • ready to play";
            else if (checking_addresses) phase = "Checking your receiving address...";
            else if (m_remote_slot < 0 || m_address[m_remote_slot]->GetValue().empty()) phase = "Enter your receiving address";
            else if (view.paid_sats[m_remote_slot]) phase = "Your entry confirmed • waiting for partner";
            else if (!m_addresses_dirty[m_remote_slot] && view.verification_state[m_remote_slot] == 2 &&
                view.invoices[m_remote_slot][0]) phase = "Scan to pay your entry";
            else phase = "Awaiting your entry invoice...";
        }
        else if (view.phase == 0) phase = "Connecting wallet...";
        else if (view.phase == 7 || view.error[0]) phase = "Payment needs attention";
        else if (view.phase == 6) phase = wxString::Format("Settling %llu sats...",
            static_cast<unsigned long long>(view.pending_sats[0] + view.pending_sats[1] + view.pending_sats[2] + view.pending_sats[3]));
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
        if (funding && !addresses_saved && (m_remote || css.ready)) {
            for (int i = 0; i < 4; ++i) {
                if (!Pays(view, i) || (m_remote && i != m_remote_slot)) continue;
                if (!m_auto_failure[i].empty()) { warning = wxString::Format("Player %d: %s", i + 1, m_auto_failure[i]); break; }
                if (m_address[i]->GetValue().empty()) { warning = wxString::Format("Player %d: enter a Lightning address.", i + 1); break; }
            }
        }
        m_address_warning->SetLabel(warning);
        m_address_warning->Wrap(CONTENT_WIDTH);
        m_address_warning->Show(!warning.empty());
        const wxString entry_rule = view.phase == 0 ? wxString("...") : wxString::Format("%llu sats", static_cast<unsigned long long>(view.entry_sats));
        const wxString rate_rule = view.phase == 0 ? wxString("...") : wxString::Format("%llu %s / 1%%", static_cast<unsigned long long>(view.sats_per_percent), view.sats_per_percent == 1 ? "sat" : "sats");
        m_entry_rule->SetLabel(wxString::Format("%llu into play + %llu fees", static_cast<unsigned long long>(view.entry_sats), static_cast<unsigned long long>(view.surcharge_sats)));
        m_entry_rule->SetToolTip(entry_rule + " per player with a wallet");
        m_rate_rule->SetLabel(rate_rule);
        m_rate_rule->SetToolTip(wxString::Format("%s, paid in %llu-second batches. The remaining pot settles after the match.", rate_rule, static_cast<unsigned long long>(view.batch_window_ms / 1000)));
        m_invoice_rule->SetLabel(wxString::Format("%llu sats", static_cast<unsigned long long>(view.invoice_sats)));
        m_invoice_rule->Show(view.phase != 0);
        m_wallet_mode->SetLabel(view.sandbox ? "SANDBOX" : (view.simulated ? "FAKE WALLET" : "SERVER WALLET"));
        m_wallet_mode->SetToolTip(m_remote ? "The room server holds entries and handles settlement. Your wallet connection stays private." :
            "Development wallet: no real bitcoin is transferred.");
        m_wallet_mode->Show(view.phase != 0 && (m_remote || view.sandbox || view.simulated));
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
            if (m_remote && i == m_remote_slot) label += " • You";
            m_player[i]->SetLabel(label);
            m_player[i]->SetForegroundColour(teams && team < 3 ? TEAM_COLOURS[team] : PLAYER_COLOURS[i]);
            m_entry_status[i]->SetLabel(!financial ? "NO ENTRY" : (paid ? "PAID" : "UNPAID"));
            m_entry_status[i]->SetForegroundColour(!financial ? MUTED : (paid ? GREEN : ACCENT));
            m_entry_status[i]->SetToolTip(financial ? wxString::Format("%s: %llu sats", paid ? "Entry received" : "Entry to pay", static_cast<unsigned long long>(view.invoice_sats)) : "CPU wallets are disabled for this match");
            const unsigned status = m_addresses_dirty[i] || (m_remote && m_address_pending[i]) ? 0 : view.verification_state[i];
            const bool compact_address = status == 2 && !m_editing_address[i] && !m_addresses_dirty[i] && !m_address[i]->HasFocus();
            m_address[i]->Show(financial && !compact_address);
            m_address_display[i]->SetLabel(m_address[i]->GetValue());
            m_address_display[i]->InvalidateBestSize();
            m_address_display[i]->SetToolTip(m_address[i]->GetValue());
            m_address_display[i]->Show(financial && compact_address);
            const bool editable = financial && funding && !paid && (m_remote ? i == m_remote_slot : !view.sandbox);
            m_edit_address[i]->Show(editable && compact_address);
            m_verify[i]->Show(editable && !m_auto_failure[i].empty());
            // Keep focus, selection and caret intact while asynchronous validation runs.
            m_address[i]->Enable(editable);
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
            else if (m_remote && i != m_remote_slot && m_address[i]->GetValue().empty()) verification = "Waiting for player's receiving address";
            else verification = m_address[i]->GetValue().empty() ? "Enter a Lightning address" : "Waiting for automatic check...";
            m_verification[i]->SetLabel(verification);
            m_verification[i]->Wrap(PLAYER_WIDTH);
            m_verification[i]->SetForegroundColour(colour);
            m_verification[i]->SetToolTip(status == 2 ? "Receiving address verified automatically; reused for up to 30 minutes. No funds sent for verification." : wxString::FromUTF8(view.verification_message[i]));
            const bool show = financial && (!m_remote || i == m_remote_slot) && !m_addresses_dirty[i] && status == 2 && !m_admitted && funding;
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
            m_paid_out[i]->SetForegroundColour(view.earned_sats[i] ? GREEN : TEXT_COLOUR);
            m_pending[i]->SetLabel(wxString::Format("%llu sats", static_cast<unsigned long long>(view.pending_sats[i])));
            m_pending[i]->SetForegroundColour(view.pending_sats[i] ? ACCENT : MUTED);
            m_pending_delay[i]->SetLabel(view.batch_remaining_ms[i] ? wxString::Format("in %llus", static_cast<unsigned long long>((view.batch_remaining_ms[i] + 999) / 1000)) : wxString("Sending..."));
            m_pending_delay[i]->Show(financial && view.pending_sats[i] != 0);
            m_pay[i]->SetToolTip(view.sandbox ? "Pay from this player's Sandbox test wallet" : "Simulate this player's entry payment");
            m_pay[i]->Show(!m_remote && financial && !paid && (view.simulated || view.sandbox) && show);
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
		if (!m_remote && (m_admitted || m_in_match) && (m_capture_failed || match.credit_stream_incomplete)) activity = "Incomplete match data • unspent funds will be returned\n";
		activity += wxString::FromUTF8(view.payment_log);
		if (activity.empty()) activity = "No payments yet.";
		if (m_log->GetValue() != activity) { m_log->ChangeValue(activity); m_log->ShowPosition(m_log->GetLastPosition()); }
        m_summary_available = view.summary_available != 0;
        const auto summary_id = wxString::FromUTF8(view.summary_match_id);
        if (m_summary_available && summary_id != m_summary_id) {
            m_summary_id = summary_id;
            BuildSummary(view);
            m_summary_auto = m_show_next_summary && !m_view_callback;
            m_show_next_summary = false;
            m_summary_start_request = css.start_requests;
            m_summary_manual = false;
        }
        // Some Melee builds skip results; keep the summary through CSS until Start.
        if (css.start_requests != m_summary_start_request) m_summary_manual = m_summary_auto = false;
        if (m_in_match || m_admitted) m_summary_manual = m_summary_auto = false;
        m_last_match->Enable(m_summary_available && !m_in_match && !m_admitted);
        LayoutSummary();
        PublishView(view);
	}

	ArenaLocalHandle* m_handle = nullptr;
	wxTimer m_timer;
    ViewCallback m_view_callback;
    bool m_strip = false, m_next_setup_requested = false;
    std::uint64_t m_result_start_request = 0;
    bool m_remote = false;
    std::weak_ptr<SatsArenaRoomClient> m_remote_client;
    int m_remote_slot = -1;
    wxString m_remote_notice, m_remote_error_at_submit;
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
    wxPanel* m_room_options = nullptr;
    wxChoice* m_room_count = nullptr;
    wxChoice* m_room_mode = nullptr;
    wxChoice* m_room_teams[4] = {};
    wxStaticText* m_room_team_labels[4] = {};
    wxStaticText* m_room_config_note = nullptr;
    nlohmann::json m_room_display_config = nullptr;
	wxStaticText* m_phone_room = nullptr;
	wxPanel* m_phone_join = nullptr;
	wxStaticText* m_phone_website = nullptr;
	wxStaticText* m_phone_code = nullptr;
	bool m_refresh_header = false;
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
wxWindow* CreateSidebar(wxWindow* parent, ViewCallback view) { return new FundingPanel(parent, std::move(view)); }
void FocusPlayer(wxWindow* sidebar, int player) { static_cast<FundingPanel*>(sidebar)->FocusPlayer(player); }
void NextSetup(wxWindow* sidebar) { static_cast<FundingPanel*>(sidebar)->NextSetup(); }
}
