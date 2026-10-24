// Copyright 2026 Sats Arena contributors
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <wx/panel.h>
#include <wx/timer.h>
#include "Core/Slippi/SatsArenaTracker.h"

class wxTextCtrl;
class wxCheckBox;

// All widgets stay on the GUI thread; gameplay arrives as bounded snapshots.
class SatsArenaPanel final : public wxPanel
{
public:
	explicit SatsArenaPanel(wxWindow* parent);
	~SatsArenaPanel() override;
	bool AcceptsFocus() const override { return false; }
	bool AcceptsFocusFromKeyboard() const override { return false; }

private:
	void OnPaint(wxPaintEvent& event);
	void LayoutActivity();
	void UpdateLog(bool force = false);
	wxTextCtrl* m_log = nullptr;
	wxCheckBox* m_follow = nullptr;
	wxTimer m_timer;
	SatsArena::Snapshot m_snapshot;
};
