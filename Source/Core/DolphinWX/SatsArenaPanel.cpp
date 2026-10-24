// Copyright 2026 Sats Arena contributors
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "DolphinWX/SatsArenaPanel.h"

#include <algorithm>
#include <wx/dcbuffer.h>
#include <wx/settings.h>

namespace
{
const wxColour BACKGROUND(16, 20, 25);
const wxColour TEXT(244, 246, 248);
const wxColour MUTED(142, 152, 165);
const wxColour BORDER(44, 52, 62);
const wxColour GREEN(198, 247, 118);

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
	const bool compact = height < 150;
	Box(dc, x, y, width, height, fill);
	Box(dc, x + 15, y + 15, 32, 32, fill, accent, 7);
	Label(dc, wxString::Format("P%d", number), x + 22, y + 23, 9, accent, true);
	Label(dc, number == 1 ? "Player one" : "Player two", x + 59, y + 15, 11, TEXT, true);
	Label(dc, "Preview balance", x + 59, y + 34, 9, MUTED);
	Label(dc, "10,000", x + 15, y + (compact ? 57 : 66), compact ? 25 : 29, TEXT, true);
	const int balance_width = dc.GetTextExtent("10,000").GetWidth();
	Label(dc, "sats", x + 23 + balance_width, y + (compact ? 76 : 86), 10, MUTED);
	Label(dc, "PREVIEW POT", x + 15, y + height - 40, 8, MUTED);
	Label(dc, "+/- 0 this match", x + width - 112, y + height - 40, 8, MUTED);
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(BORDER));
	dc.DrawRectangle(x + 15, y + height - 16, width - 30, 3);
	dc.SetBrush(wxBrush(accent));
	dc.DrawRectangle(x + 15, y + height - 16, (width - 30) / 2, 3);
}
}

SatsArenaPanel::SatsArenaPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(350, -1), 0)
{
	SetMinSize(wxSize(350, -1));
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetBackgroundColour(BACKGROUND);
	SetName("Sats Arena UI preview. Placeholder balances. No real money.");
	SetToolTip("Native sidebar preview. Lightning and live game tracking are not connected.");
	Bind(wxEVT_PAINT, &SatsArenaPanel::OnPaint, this);
	Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
		Refresh(false);
		event.Skip();
	});
}

void SatsArenaPanel::OnPaint(wxPaintEvent&)
{
	wxAutoBufferedPaintDC dc(this);
	dc.SetBackground(wxBrush(BACKGROUND));
	dc.Clear();
	// Scale the preview on very short windows instead of clipping its safety notice.
	const double scale = std::min(1.0, GetClientSize().GetHeight() / 640.0);
	if (scale <= 0)
		return;
	dc.SetUserScale(scale, scale);
	const int width = static_cast<int>(GetClientSize().GetWidth() / scale);
	const int height = static_cast<int>(GetClientSize().GetHeight() / scale);
	const bool compact = height < 800;
	const int margin = 20;
	const int content_width = width - margin * 2;
	const int card_height = compact ? 132 : 164;
	const int summary_y = compact ? 74 : 90;

	dc.SetPen(wxPen(BORDER));
	dc.DrawLine(0, 0, 0, height);
	Box(dc, margin, 22, 32, 36, wxColour(29, 37, 27), wxColour(63, 77, 47), 8);
	Label(dc, "S", margin + 9, 28, 17, GREEN, true);
	Label(dc, "SATS ARENA", margin + 44, 23, 12, TEXT, true);
	Label(dc, "A little more on the line.", margin + 44, 43, 8, MUTED);
	Box(dc, width - 91, 26, 71, 22, BACKGROUND, BORDER, 4);
	Label(dc, "UI PREVIEW", width - 85, 32, 7, MUTED);

	Label(dc, "LOCAL VERSUS", margin, summary_y, 8, MUTED, true);
	Label(dc, "OFFLINE", width - 70, summary_y, 8, MUTED);
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

	// Keep payment placeholders and the safety notice visible on shorter displays.
	const int footer_y = std::max(y, height - 143);
	const int activity_height = footer_y - y - 18;
	if (activity_height >= 70)
	{
		Label(dc, "Match activity", margin, y, 10, TEXT, true);
		Label(dc, "0 EVENTS", width - 78, y + 2, 8, MUTED);
		Box(dc, margin, y + 26, content_width, activity_height - 24, BACKGROUND);
		Label(dc, "Waiting for the first hit.", margin + 18, y + 43, 10, MUTED);
		if (activity_height > 105)
			Label(dc, "Live Slippi data is not connected yet.", margin + 18, y + 65, 8, MUTED);
	}

	const int button_width = (content_width - 10) / 2;
	Box(dc, margin, footer_y, button_width, 36, wxColour(30, 40, 27), wxColour(47, 59, 42), 7);
	Label(dc, "Fund pot (soon)", margin + 17, footer_y + 12, 9, wxColour(140, 157, 117));
	Box(dc, margin + button_width + 10, footer_y, button_width, 36, wxColour(27, 32, 39), BORDER, 7);
	Label(dc, "Withdraw (soon)", margin + button_width + 25, footer_y + 12, 9, MUTED);
	Label(dc, "Just the interface. No real money.", margin, footer_y + 53, 9, TEXT, true);
	Label(dc, "Balances are placeholders. Lightning and", margin, footer_y + 73, 8, MUTED);
	Label(dc, "game tracking are not connected.", margin, footer_y + 89, 8, MUTED);
	Label(dc, "BUILT FOR FRIENDLY RIVALRIES", margin, footer_y + 119, 7, MUTED);
}
