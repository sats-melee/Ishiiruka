// Copyright 2026 Sats Arena contributors
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "DolphinWX/SatsArenaPanel.h"

#include <algorithm>
#include <utility>
#include <wx/dcbuffer.h>
#include <wx/settings.h>
#include <wx/textctrl.h>

namespace
{
const wxColour BACKGROUND(16, 20, 25);
const wxColour TEXT(244, 246, 248);
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
           const wxColour& colour = TEXT, bool bold = false)
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
	Label(dc, number == 1 ? "Player one" : "Player two", x + 59, y + 15, 11, TEXT, true);
	Label(dc, "Preview balance", x + 59, y + 34, 9, MUTED);
	Label(dc, "10,000", x + 15, y + (compact ? 51 : 58), compact ? 22 : 25, TEXT, true);
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
	m_log->SetForegroundColour(TEXT);
	m_log->SetFont(wxFont(9, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
	m_log->SetName("Finalized player damage log");
	Bind(wxEVT_PAINT, &SatsArenaPanel::OnPaint, this);
	Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
		LayoutActivity();
		Refresh(false);
		event.Skip();
	});
	Bind(wxEVT_TIMER, [this](wxTimerEvent&) { UpdateLog(); });
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
	Label(dc, "SATS ARENA", margin + 44, 23, 12, TEXT, true);
	Label(dc, "A little more on the line.", margin + 44, 43, 8, MUTED);
	Box(dc, width - 91, 26, 71, 22, BACKGROUND, BORDER, 4);
	Label(dc, "UI PREVIEW", width - 85, 32, 7, MUTED);

	Label(dc, "LOCAL VERSUS", margin, summary_y, 8, MUTED, true);
	Label(dc, StateLabel(m_snapshot.status), width - 89, summary_y, 8, MUTED);
	Label(dc, "Every hit counts.", margin, summary_y + 23, compact ? 19 : 23, TEXT, true);
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
	Label(dc, "Damage log", margin, y, 10, TEXT, true);
	Label(dc, "AUTO-SCROLL", width - 103, y + 2, 8, MUTED);
	Label(dc, wxString::Format("%llu credited | showing last %u",
	                          static_cast<unsigned long long>(m_snapshot.total_credit_events),
	                          static_cast<unsigned>(SatsArena::MAX_CREDIT_MESSAGES)),
	      margin, y + 20, 8, MUTED);

	const int button_width = (content_width - 10) / 2;
	Box(dc, margin, footer_y, button_width, 36, wxColour(30, 40, 27), wxColour(47, 59, 42), 7);
	Label(dc, "Fund pot (soon)", margin + 17, footer_y + 12, 9, wxColour(140, 157, 117));
	Box(dc, margin + button_width + 10, footer_y, button_width, 36, wxColour(27, 32, 39), BORDER, 7);
	Label(dc, "Withdraw (soon)", margin + button_width + 25, footer_y + 12, 9, MUTED);
	Label(dc, "Live damage. No real money.", margin, footer_y + 53, 9, TEXT, true);
	Label(dc, m_snapshot.credit_stream_seen ? "Game credits are nominal, not applied %." : "Credit hook: waiting for local match data.",
	      margin, footer_y + 73, 8, MUTED);
	Label(dc, "Pots are placeholders. Lightning is offline.", margin, footer_y + 89, 8, MUTED);
	Label(dc, "BUILT FOR FRIENDLY RIVALRIES", margin, footer_y + 119, 7, MUTED);
}
