#ifdef SATS_ARENA_LOCAL
#include "DolphinWX/SatsArenaPanel.h"
#include "DolphinWX/SatsArenaLocal.h"
#include "DolphinWX/SatsArenaOnline.h"
#include "Core/Slippi/SatsArenaNetplayRoom.h"
#include "sats_arena_local.h"
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

SatsArenaPanel::SatsArenaPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY), m_timer(this)
{
    m_online = SatsArenaNetplay::ServerEnabled();
    SetMinSize(wxSize(m_online ? 256 : 480, -1));
    SetBackgroundColour(wxColour(14, 20, 30));
    auto* root = new wxBoxSizer(wxVERTICAL);
    SetSizer(root);
    m_restore = new wxButton(this, wxID_ANY, "‹", wxDefaultPosition, wxSize(28, -1), wxBU_EXACTFIT);
    m_restore->SetMinSize(wxSize(28, -1));
    m_restore->SetName(m_online ? "Show multiplayer panel" : "Show local match panel");
    m_restore->SetToolTip("Show match panel (Ctrl+Shift+M)");
    m_restore->Bind(wxEVT_BUTTON, std::function<void(wxCommandEvent&)>([this](wxCommandEvent&) { ToggleExpanded(); }));
    root->Add(m_restore, 1, wxEXPAND);
    m_restore->Hide();
    if (m_online)
    {
        m_funding = SatsArenaOnline::CreateSidebar(this, [this] { ToggleExpanded(); },
            [this](const wxString& notice, bool attention) {
                m_notice = notice;
                m_attention = attention;
                m_restore->SetLabel(attention ? "!" : "‹");
                m_restore->SetForegroundColour(attention ? wxColour(255, 204, 72) : *wxWHITE);
                m_restore->SetToolTip(notice + "\nShow multiplayer panel (Ctrl+Shift+M)");
                if (m_changed) m_changed();
            });
    }
    else
    {
        m_details = new wxDialog(this, wxID_ANY, "Local payments & results", wxDefaultPosition,
                                wxSize(510, 640), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
        m_details->SetMinSize(wxSize(500, 360));
        m_details->SetSizer(new wxBoxSizer(wxVERTICAL));
        m_details->Bind(wxEVT_CLOSE_WINDOW, std::function<void(wxCloseEvent&)>([this](wxCloseEvent& event) {
            if (!event.CanVeto()) { event.Skip(); return; }
            m_details->Hide(); event.Veto();
        }));
        m_strip = new wxPanel(this, wxID_ANY);
        m_strip->SetName("Local match bottom strip");
        m_strip->SetBackgroundColour(wxColour(21, 33, 51));
        m_strip->SetMinSize(wxSize(-1, 64));
        m_strip->SetMaxSize(wxSize(-1, 64));
        auto* strip = new wxBoxSizer(wxHORIZONTAL);
        auto* summary = new wxBoxSizer(wxVERTICAL);
        m_strip_status = new wxStaticText(m_strip, wxID_ANY, "LOCAL MATCH");
        m_strip_status->SetForegroundColour(wxColour(237, 241, 247));
        m_strip_status->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_SLANT, wxFONTWEIGHT_BOLD));
        m_strip_total = new wxStaticText(m_strip, wxID_ANY, "");
        m_strip_total->SetForegroundColour(wxColour(161, 178, 199));
        m_strip_total->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        summary->Add(m_strip_status, 0, wxBOTTOM, 5);
        summary->Add(m_strip_total, 0);
        strip->Add(summary, 0, wxALL | wxALIGN_CENTER_VERTICAL, 10);
        for (int i = 0; i < 4; ++i)
        {
            auto* button = new wxButton(m_strip, wxID_ANY, "", wxDefaultPosition,
                                        wxSize(110, 56), wxBU_EXACTFIT);
            button->SetMinSize(wxSize(110, 56));
            button->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
            button->SetBackgroundColour(wxColour(21, 33, 51));
            button->Bind(wxEVT_BUTTON, std::function<void(wxCommandEvent&)>([this, i](wxCommandEvent&) { OpenDetails(i); }));
            m_strip_players[i] = button;
            strip->Add(button, 1, wxTOP | wxBOTTOM | wxRIGHT | wxEXPAND, 4);
        }
        m_payments = new wxButton(m_strip, wxID_ANY, "Payments", wxDefaultPosition,
                                  wxDefaultSize, wxBU_EXACTFIT);
        m_payments->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        m_payments->Bind(wxEVT_BUTTON, std::function<void(wxCommandEvent&)>([this](wxCommandEvent&) { OpenDetails(-1); }));
        strip->Add(m_payments, 0, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, 8);
        m_next_setup = new wxButton(m_strip, wxID_ANY, "Next setup", wxDefaultPosition,
                                        wxDefaultSize, wxBU_EXACTFIT);
        m_next_setup->SetFont(m_payments->GetFont());
        m_next_setup->SetToolTip("Open fresh entry setup. Start on a controller also continues.");
        m_next_setup->Bind(wxEVT_BUTTON, std::function<void(wxCommandEvent&)>([this](wxCommandEvent&) { SatsArenaLocal::NextSetup(m_funding); }));
        strip->Add(m_next_setup, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 8);
        m_next_setup->Hide();
        auto* hide = new wxButton(m_strip, wxID_ANY, "Hide ›", wxDefaultPosition,
                                  wxDefaultSize, wxBU_EXACTFIT);
        hide->SetFont(m_payments->GetFont());
        hide->Bind(wxEVT_BUTTON, std::function<void(wxCommandEvent&)>([this](wxCommandEvent&) { ToggleExpanded(); }));
        strip->Add(hide, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 8);
        m_strip->SetSizer(strip);
        root->Add(m_strip, 0, wxEXPAND);
        m_strip->Hide();
        m_funding = SatsArenaLocal::CreateSidebar(this,
            [this](const ArenaLocalView& view, bool compact, const wxString& notice, bool attention) {
                if (m_funding) UpdateLocal(view, compact, notice, attention);
            });
    }
    root->Add(m_funding, 1, wxEXPAND);
}

void SatsArenaPanel::SetPresentation()
{
    if (!m_online)
    {
        const bool detached = m_funding->GetParent() == m_details;
        if (m_compact && !detached)
        {
            GetSizer()->Detach(m_funding);
            m_funding->Reparent(m_details);
            m_details->GetSizer()->Add(m_funding, 1, wxEXPAND);
        }
        else if (!m_compact && detached)
        {
            m_details->Hide();
            m_details->GetSizer()->Detach(m_funding);
            m_funding->Reparent(this);
            GetSizer()->Add(m_funding, 1, wxEXPAND);
        }
        if (!m_expanded) m_details->Hide();
        m_strip->Show(m_compact && m_expanded);
        m_funding->Show(m_compact || m_expanded);
    }
    else
        m_funding->Show(m_expanded);
    m_restore->Show(!m_expanded);
    SetMinSize(!m_expanded ? wxSize(28, -1) :
        (UsesBottomStrip() ? wxSize(-1, 64) : wxSize(m_online ? 256 : 480, -1)));
    if (m_changed) m_changed();
    GetParent()->Layout();
    GetParent()->SendSizeEvent();
}

void SatsArenaPanel::ToggleExpanded()
{
    m_expanded = !m_expanded;
    // Keep the funding controls and their timer alive in either presentation.
    SetPresentation();
}

void SatsArenaPanel::OpenDetails(int player)
{
    m_details->Layout();
    if (!m_details->IsShown()) m_details->CentreOnParent();
    m_details->Show();
    m_details->Raise();
    SatsArenaLocal::FocusPlayer(m_funding, player);
}

void SatsArenaPanel::UpdateLocal(const ArenaLocalView& view, bool compact,
                                const wxString& notice, bool attention)
{
    const bool changed = m_compact != compact;
    m_compact = compact;
    const wxString next_notice = attention ? wxString("Payment needs attention") : notice;
    const bool notice_changed = m_notice != next_notice || m_attention != attention;
    m_notice = next_notice;
    m_attention = attention;
    m_restore->SetLabel(attention ? "!" : "‹");
    m_restore->SetForegroundColour(attention ? wxColour(255, 204, 72) : *wxWHITE);
    m_restore->SetToolTip(m_notice + "\nShow local match panel (Ctrl+Shift+M)");
    std::uint64_t remaining = 0, pending = 0, received = 0;
    for (int i = 0; i < 4; ++i)
    {
        remaining += view.remaining_sats[i]; pending += view.pending_sats[i]; received += view.earned_sats[i];
    }
    wxString status = attention ? wxString("PAYMENT NEEDS ATTENTION") : wxString("LOCAL MATCH");
    if (!attention && view.phase == 6) status = "SETTLING PAYMENTS";
    else if (!attention && view.phase == 4) status = "MATCH SETTLED";
    if (!attention && (view.phase == 4 || view.phase == 6) && view.summary_available)
    {
        if (view.summary_winner >= 0 && view.summary_winner < 4)
        {
            const int winner = view.summary_winner;
            status = view.summary_teams && view.summary_team_ids[winner] < 3 ?
                wxString::Format("%s TEAM WINS", std::array<const char*, 3>{{"RED", "BLUE", "GREEN"}}[view.summary_team_ids[winner]]) :
                wxString::Format("P%d WINS", winner + 1);
        }
        else status = "NO CONTEST";
    }
    m_strip_status->SetLabel(status);
    m_strip_status->SetForegroundColour(attention ? wxColour(255, 204, 72) : wxColour(237, 241, 247));
    const wxString mode = view.sandbox ? "Sandbox · " : (view.simulated ? "Simulated · " : "");
    m_strip_total->SetLabel(mode + wxString::Format(view.phase == 4 ? "%llu sats paid" :
        (view.phase == 6 ? "%llu sats pending" : "%llu sats in pot"),
        static_cast<unsigned long long>(view.phase == 4 ? received : (view.phase == 6 ? pending : remaining))));
    m_strip_total->SetToolTip(wxString::Format("%llu sats in pot · %llu sats pending · %llu sats received",
        static_cast<unsigned long long>(remaining), static_cast<unsigned long long>(pending),
        static_cast<unsigned long long>(received)));
    const std::array<wxColour, 4> ports = {{wxColour(243, 129, 135), wxColour(142, 185, 255),
                                         wxColour(255, 207, 84), wxColour(150, 214, 174)}};
    const std::array<wxColour, 3> teams = {{ports[0], ports[1], ports[3]}};
    for (int i = 0; i < 4; ++i)
    {
        auto* button = m_strip_players[i];
        button->Show(view.kinds[i] < 2);
        const bool financial = view.kinds[i] == 0 || (view.cpu_wallets && view.kinds[i] == 1);
        const unsigned team = view.team_ids[i];
        wxString name = wxString::Format("P%d · %s", i + 1, view.kinds[i] == 1 ? "CPU" : "Human");
        if (view.teams && team < 3) name += wxString::Format(" · %s", std::array<const char*, 3>{{"Red", "Blue", "Green"}}[team]);
        const wxString money = financial ? wxString::Format("+%llu sats received",
            static_cast<unsigned long long>(view.earned_sats[i])) : wxString("No wallet");
        button->SetLabel(name + "\n" + money);
        button->SetForegroundColour(view.teams && team < 3 ? teams[team] : ports[i]);
        button->SetToolTip(financial ? wxString::Format("%llu sats in pot · %llu sats pending\nOpen Player %d payments",
            static_cast<unsigned long long>(view.remaining_sats[i]),
            static_cast<unsigned long long>(view.pending_sats[i]), i + 1) : wxString("CPU without a receiving wallet"));
    }
    m_payments->SetLabel((view.phase == 4 || view.phase == 6) && view.summary_available ? "Results" : "Payments");
    m_next_setup->Show((view.phase == 4 || view.phase == 6) && view.summary_available);
    m_next_setup->Enable(SatsArena::GetPaymentGate().Read().roster_valid);
    m_strip->Layout();
    if (changed) SetPresentation();
    else if (notice_changed && m_changed) m_changed();
}

SatsArenaPanel::~SatsArenaPanel()
{
    if (m_details)
    {
        if (m_funding->GetParent() == m_details)
        {
            m_details->GetSizer()->Detach(m_funding);
            m_funding->Reparent(this);
        }
        m_details->Destroy();
    }
}

#else
// Copyright 2026 Sats Arena contributors
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "DolphinWX/SatsArenaPanel.h"

#include <algorithm>
#include <functional>
#include <utility>
#include <wx/dcbuffer.h>
#include <wx/settings.h>
#include <wx/textctrl.h>

namespace
{
// Bundled wxWidgets needs default-constructible event functors on MSVC.

const wxColour BACKGROUND(16, 20, 25);
const wxColour TEXT_COLOUR(244, 246, 248);
const wxColour MUTED(142, 152, 165);
const wxColour BORDER(44, 52, 62);
const wxColour GREEN(198, 247, 118);

struct Layout
{
	double scale;
	int width;
	int height;
	bool compact;
	int card_height;
	int summary_y;
	int activity_y;
	int footer_y;
};

Layout Measure(const wxSize& size)
{
	Layout layout;
	layout.scale = std::min(1.0, std::max(1, size.GetHeight()) / 760.0);
	layout.width = static_cast<int>(size.GetWidth() / layout.scale);
	layout.height = static_cast<int>(size.GetHeight() / layout.scale);
	layout.compact = layout.height < 900;
	layout.card_height = layout.compact ? 104 : 140;
	layout.summary_y = layout.compact ? 74 : 90;
	layout.activity_y = layout.summary_y + (layout.compact ? 64 : 82) + 59 +
	                    layout.card_height * 2 + 34;
	layout.footer_y = std::max(layout.activity_y + 140, layout.height - 143);
	return layout;
}

wxString StateLabel(SatsArena::TrackingState status)
{
	switch (status)
	{
	case SatsArena::TrackingState::LocalMatch: return "LIVE DATA";
	case SatsArena::TrackingState::Unsupported: return "LOCAL ONLY";
	case SatsArena::TrackingState::Finished: return "ENDED";
	default: return "WAITING";
	}
}

void Label(wxDC& dc, const wxString& text, int x, int y, int points,
           const wxColour& colour = TEXT_COLOUR, bool bold = false)
{
	dc.SetFont(wxFont(points, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
	                   bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL));
	dc.SetTextForeground(colour);
	dc.DrawText(text, x, y);
}

void Box(wxDC& dc, int x, int y, int width, int height, const wxColour& fill,
         const wxColour& border = BORDER, int radius = 10)
{
	dc.SetPen(wxPen(border));
	dc.SetBrush(wxBrush(fill));
	dc.DrawRoundedRectangle(x, y, width, height, radius);
}

void Player(wxDC& dc, int x, int y, int width, int height, int number,
            const wxColour& accent, const wxColour& fill)
{
	const bool compact = height < 125;
	Box(dc, x, y, width, height, fill);
	Box(dc, x + 15, y + 15, 32, 32, fill, accent, 7);
	Label(dc, wxString::Format("P%d", number), x + 22, y + 23, 9, accent, true);
	Label(dc, number == 1 ? "Player one" : "Player two", x + 59, y + 15, 11, TEXT_COLOUR, true);
	Label(dc, "Preview balance", x + 59, y + 34, 9, MUTED);
	Label(dc, "10,000", x + 15, y + (compact ? 51 : 58), compact ? 22 : 25, TEXT_COLOUR, true);
	const int balance_width = dc.GetTextExtent("10,000").GetWidth();
	Label(dc, "sats", x + 23 + balance_width, y + (compact ? 65 : 77), 10, MUTED);
	if (!compact)
	{
		Label(dc, "PREVIEW POT", x + 15, y + height - 34, 8, MUTED);
		Label(dc, "+/- 0 this match", x + width - 112, y + height - 34, 8, MUTED);
	}
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(BORDER));
	dc.DrawRectangle(x + 15, y + height - 16, width - 30, 3);
	dc.SetBrush(wxBrush(accent));
	dc.DrawRectangle(x + 15, y + height - 16, (width - 30) / 2, 3);
}
}

SatsArenaPanel::SatsArenaPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(350, -1), 0)
    , m_timer(this)
{
	SetMinSize(wxSize(350, -1));
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetBackgroundColour(BACKGROUND);
	SetName("Sats Arena. Live local damage log. Placeholder balances. No real money.");
	SetToolTip("Finalized game-credited damage. Nominal amounts, not applied percent. No payments.");
	m_log = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
	                       wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE);
	m_log->SetBackgroundColour(wxColour(20, 25, 31));
	m_log->SetForegroundColour(TEXT_COLOUR);
	m_log->SetFont(wxFont(9, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
	m_log->SetName("Finalized player damage log");
	Bind(wxEVT_PAINT, &SatsArenaPanel::OnPaint, this);
	Bind(wxEVT_SIZE, std::function<void(wxSizeEvent&)>([this](wxSizeEvent& event) {
		LayoutActivity();
		Refresh(false);
		event.Skip();
	}));
	Bind(wxEVT_TIMER, std::function<void(wxTimerEvent&)>([this](wxTimerEvent&) { UpdateLog(); }));
	LayoutActivity();
	UpdateLog(true);
	m_timer.Start(100);
}

SatsArenaPanel::~SatsArenaPanel()
{
	m_timer.Stop();
}

void SatsArenaPanel::LayoutActivity()
{
	if (!m_log)
		return;
	const auto layout = Measure(GetClientSize());
	const auto px = [&layout](int value) { return static_cast<int>(value * layout.scale); };
	m_log->SetSize(px(22), px(layout.activity_y + 40), px(layout.width - 44),
	               px(layout.footer_y - layout.activity_y - 54));
	m_log->ShowPosition(m_log->GetLastPosition());
}

void SatsArenaPanel::UpdateLog(bool force)
{
	auto snapshot = SatsArena::GetTracker().Read();
	if (!force && snapshot.revision == m_snapshot.revision)
		return;
	m_snapshot = std::move(snapshot);
	Refresh(false);
	wxString text;
	if (m_snapshot.credit_stream_incomplete)
		text += "WARNING: credit capture incomplete.\nNot suitable for accounting.\n\n";
	if (m_snapshot.credit_events.empty())
	{
		if (m_snapshot.status == SatsArena::TrackingState::Unsupported)
			text += "Tracking supports local two-player VS only.\n\nOnline, teams and demos are excluded.";
		else if (m_snapshot.status == SatsArena::TrackingState::LocalMatch)
			text += "Match started. Waiting for game credits...";
		else if (m_snapshot.status == SatsArena::TrackingState::Finished)
			text += "Match ended. No game credits recorded.";
		else
			text += "Start a local two-player VS match.\n\nFinalized game credits will appear here.";
	}
	for (const auto& event : m_snapshot.credit_events)
	{
		text += wxString::Format("f%d  CREDIT P%d%s -> P%d%s +%.1f\n", event.frame,
		                         event.attacker + 1, event.attacker_follower ? "(Nana)" : "",
		                         event.victim + 1, event.victim_follower ? "(Nana)" : "", event.amount);
		text += wxString::Format("  Attack %u, #%u\n\n",
		                         event.attack_id, unsigned(event.ordinal));
	}
	m_log->ChangeValue(text);
	// Follow the newest message on every update without taking focus from the game.
	m_log->SetInsertionPointEnd();
	m_log->ShowPosition(m_log->GetLastPosition());
}

void SatsArenaPanel::OnPaint(wxPaintEvent&)
{
	wxAutoBufferedPaintDC dc(this);
	dc.SetBackground(wxBrush(BACKGROUND));
	dc.Clear();
	const auto layout = Measure(GetClientSize());
	dc.SetUserScale(layout.scale, layout.scale);
	const int width = layout.width;
	const bool compact = layout.compact;
	const int margin = 20;
	const int content_width = width - margin * 2;
	const int card_height = layout.card_height;
	const int summary_y = layout.summary_y;

	dc.SetPen(wxPen(BORDER));
	dc.DrawLine(0, 0, 0, layout.height);
	Box(dc, margin, 22, 32, 36, wxColour(29, 37, 27), wxColour(63, 77, 47), 8);
	Label(dc, "S", margin + 9, 28, 17, GREEN, true);
	Label(dc, "SATS ARENA", margin + 44, 23, 12, TEXT_COLOUR, true);
	Label(dc, "A little more on the line.", margin + 44, 43, 8, MUTED);
	Box(dc, width - 91, 26, 71, 22, BACKGROUND, BORDER, 4);
	Label(dc, "UI PREVIEW", width - 85, 32, 7, MUTED);

	Label(dc, "LOCAL VERSUS", margin, summary_y, 8, MUTED, true);
	Label(dc, StateLabel(m_snapshot.status), width - 89, summary_y, 8, MUTED);
	Label(dc, "Every hit counts.", margin, summary_y + 23, compact ? 19 : 23, TEXT_COLOUR, true);
	int y = summary_y + (compact ? 64 : 82);
	Box(dc, margin, y, content_width, 43, wxColour(23, 29, 35));
	Label(dc, "Damage rate", margin + 12, y + 15, 9, MUTED);
	Label(dc, "10 sats / 1%", width - 124, y + 14, 10, GREEN, true);
	y += 59;

	Player(dc, margin, y, content_width, card_height, 1, wxColour(242, 153, 174), wxColour(30, 28, 35));
	y += card_height + 12;
	Player(dc, margin, y, content_width, card_height, 2, wxColour(136, 183, 255), wxColour(25, 34, 46));
	y += card_height + 22;

	const int footer_y = layout.footer_y;
	Label(dc, "Damage log", margin, y, 10, TEXT_COLOUR, true);
	Label(dc, "AUTO-SCROLL", width - 103, y + 2, 8, MUTED);
	Label(dc, wxString::Format("%llu credited | showing last %u",
	                          static_cast<unsigned long long>(m_snapshot.total_credit_events),
	                          static_cast<unsigned>(SatsArena::MAX_CREDIT_MESSAGES)),
	      margin, y + 20, 8, MUTED);

#ifndef SATS_ARENA_LOCAL
	const int button_width = (content_width - 10) / 2;
	Box(dc, margin, footer_y, button_width, 36, wxColour(30, 40, 27), wxColour(47, 59, 42), 7);
	Label(dc, "Fund pot (soon)", margin + 17, footer_y + 12, 9, wxColour(140, 157, 117));
	Box(dc, margin + button_width + 10, footer_y, button_width, 36, wxColour(27, 32, 39), BORDER, 7);
	Label(dc, "Withdraw (soon)", margin + button_width + 25, footer_y + 12, 9, MUTED);
#endif
	Label(dc, "Live damage. No real money.", margin, footer_y + 53, 9, TEXT_COLOUR, true);
	Label(dc, m_snapshot.credit_stream_seen ? "Game credits are nominal, not applied %." : "Credit hook: waiting for local match data.",
	      margin, footer_y + 73, 8, MUTED);
	Label(dc, "Pots are placeholders. Lightning is offline.", margin, footer_y + 89, 8, MUTED);
	Label(dc, "BUILT FOR FRIENDLY RIVALRIES", margin, footer_y + 119, 7, MUTED);
}

#endif
