// Copyright 2026 Sats Arena contributors
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <utility>
#include <wx/panel.h>
#include <wx/timer.h>
#include "Core/Slippi/SatsArenaTracker.h"

class wxTextCtrl;
class wxButton;
class wxDialog;
class wxStaticText;
struct ArenaLocalView;

// All widgets stay on the GUI thread; gameplay arrives as bounded snapshots.
class SatsArenaPanel final : public wxPanel
{
public:
	explicit SatsArenaPanel(wxWindow* parent);
#ifdef SATS_ARENA_LOCAL
	bool IsOnline() const { return m_online; }
	bool UsesBottomStrip() const { return !m_online && m_compact && m_expanded; }
	bool IsExpanded() const { return m_expanded; }
	void ToggleExpanded();
	void SetChangedCallback(std::function<void()> callback) { m_changed = std::move(callback); }
	const wxString& Notice() const { return m_notice; }
	bool NeedsAttention() const { return m_attention; }
#endif
	~SatsArenaPanel() override;
#ifndef SATS_ARENA_LOCAL
	bool AcceptsFocus() const override { return false; }
	bool AcceptsFocusFromKeyboard() const override { return false; }
#endif

private:
	void OnPaint(wxPaintEvent& event);
	void LayoutActivity();
	void UpdateLog(bool force = false);
	wxTextCtrl* m_log = nullptr;
#ifdef SATS_ARENA_LOCAL
	void UpdateLocal(const ArenaLocalView& view, bool compact, const wxString& notice, bool attention);
    void SetPresentation();
    void OpenDetails(int player);
    wxWindow* m_funding = nullptr;
    wxPanel* m_strip = nullptr;
    wxStaticText* m_strip_status = nullptr;
    wxStaticText* m_strip_total = nullptr;
    wxButton* m_strip_players[4] = {};
    wxButton* m_payments = nullptr;
    wxButton* m_next_setup = nullptr;
    wxDialog* m_details = nullptr;
    bool m_compact = false;
	wxButton* m_restore = nullptr;
	bool m_online = false;
	bool m_expanded = true;
	bool m_attention = false;
	wxString m_notice;
	std::function<void()> m_changed;
#endif
	wxTimer m_timer;
	SatsArena::Snapshot m_snapshot;
};
