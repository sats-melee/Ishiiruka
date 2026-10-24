// Copyright 2026 Sats Arena contributors. Licensed under GPLv2+.
#pragma once
#include <string>
class wxWindow;

namespace SatsArenaLocal
{
// Fake-only funding preview. No wallet credentials or HTTP service.
// The first gate is at emulator boot, NOT at every in-game stage selection.
bool AllowBoot(wxWindow* parent, const std::string& filename);
void ShowFunding(wxWindow* parent);
}
