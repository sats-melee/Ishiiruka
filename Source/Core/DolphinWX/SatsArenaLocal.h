// Copyright 2026 Sats Arena contributors. Licensed under GPLv2+.
#pragma once
class wxWindow;
namespace SatsArenaLocal
{
// Embedded sidebar; all payments run on the Rust worker.
wxWindow* CreateSidebar(wxWindow* parent);
}
