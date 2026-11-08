// Copyright 2026 Sats Arena contributors. Licensed under GPLv2+.
#pragma once
#include <functional>
#include <wx/string.h>
class wxWindow;
struct ArenaLocalView;
namespace SatsArenaLocal
{
// Embedded funding controls and compact snapshots share the existing Rust worker.
using ViewCallback = std::function<void(const ArenaLocalView&, bool, const wxString&, bool)>;
wxWindow* CreateSidebar(wxWindow* parent, ViewCallback view = {});
void FocusPlayer(wxWindow* sidebar, int player);
void NextSetup(wxWindow* sidebar);
}
