// Copyright 2026 Sats Arena contributors
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <wx/panel.h>

// UI preview only. Paints on the GUI thread; never touches emulation or wallet state.
class SatsArenaPanel final : public wxPanel
{
public:
	explicit SatsArenaPanel(wxWindow* parent);
	bool AcceptsFocus() const override { return false; }
	bool AcceptsFocusFromKeyboard() const override { return false; }

private:
	void OnPaint(wxPaintEvent& event);
};
