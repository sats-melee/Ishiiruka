// Copyright 2026 Sats Arena contributors. Licensed under GPLv2+.
#pragma once

#include <algorithm>
#include <wx/bitmap.h>
#include <wx/dcmemory.h>
#include <wx/string.h>
#include "DolphinWX/QR/qrcodegen.hpp"

namespace SatsArenaWidgets
{
inline wxBitmap InvoiceQR(const char* invoice)
{
    if (!invoice || !*invoice) return wxBitmap(1, 1);
    // BOLT11 accepts uppercase, which uses fewer QR modules and keeps them legible.
    const auto qr_text = wxString::FromUTF8(invoice).Upper();
    const auto qr = qrcodegen::QrCode::encodeText(qr_text.utf8_str().data(), qrcodegen::QrCode::Ecc::MEDIUM);
    const int modules = qr.getSize();
    const int quiet = 4;
    const int scale = std::max(1, 148 / (modules + quiet * 2));
    // Use an exact module grid: only the required four-module quiet zone,
    // with no leftover white padding or blurry fractional scaling.
    const int size = (modules + quiet * 2) * scale;
    wxBitmap bitmap(size, size);
    wxMemoryDC dc(bitmap);
    dc.SetBackground(*wxWHITE_BRUSH);
    dc.Clear();
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(*wxBLACK_BRUSH);
    for (int y = 0; y < modules; ++y)
        for (int x = 0; x < modules; ++x)
            if (qr.getModule(x, y))
                dc.DrawRectangle((quiet + x) * scale, (quiet + y) * scale, scale, scale);
    dc.SelectObject(wxNullBitmap);
    return bitmap;
}

}
