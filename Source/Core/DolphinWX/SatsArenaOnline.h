// Copyright 2026 Sats Arena contributors. Licensed under GPLv2+.
#pragma once

#include <functional>
#include <wx/string.h>

class wxWindow;

namespace SatsArenaOnline
{
using NoticeCallback = std::function<void(const wxString&, bool attention)>;

// Visibility is owned by the frame; room polling continues while hidden.
wxWindow* CreateSidebar(wxWindow* parent, std::function<void()> hide,
                        NoticeCallback notice = {});
}
