// Copyright 2026 Sats Arena contributors. Licensed under GPLv2+.
#include "DolphinWX/SatsArenaOnline.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <memory>
#include <utility>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/dialog.h>
#include <wx/msgdlg.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>

#include "Common/Logging/Log.h"
#include "Core/Slippi/SatsArenaRoomClient.h"
#include "DolphinWX/SatsArenaRoomView.h"
#include "DolphinWX/SatsArenaWidgets.h"

namespace SatsArenaOnline
{
namespace
{
const wxColour BACKGROUND(16, 24, 36);
const wxColour SURFACE(26, 38, 54);
const wxColour BORDER(43, 57, 76);
const wxColour TEXT_COLOUR(237, 241, 247);
const wxColour MUTED(149, 163, 183);
const wxColour GOLD(255, 207, 84);
const wxColour GREEN(150, 214, 174);
const wxColour WARNING(246, 185, 97);
const std::array<wxColour, 4> PORTS = {{wxColour(243, 129, 135), wxColour(142, 185, 255),
                                     GOLD, GREEN}};
const std::array<wxColour, 3> TEAMS = {{PORTS[0], PORTS[1], GREEN}};
constexpr int WIDTH = 256;
constexpr int INSET = 8;

wxStaticText* Label(wxWindow* parent, const wxString& text, int points = 9,
                    const wxColour& colour = TEXT_COLOUR, bool bold = false, long style = 0)
{
    auto* label = new wxStaticText(parent, wxID_ANY, text, wxDefaultPosition, wxDefaultSize, style);
    label->SetForegroundColour(colour);
    label->SetFont(wxFont(points, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                         bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL));
    return label;
}

wxButton* Button(wxWindow* parent, const wxString& text, int points = 9)
{
    auto* button = new wxButton(parent, wxID_ANY, text, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    button->SetFont(wxFont(points, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    button->SetBackgroundColour(SURFACE);
    button->SetForegroundColour(TEXT_COLOUR);
    button->SetMinSize(wxSize(-1, 24));
    return button;
}

wxPanel* Divider(wxWindow* parent)
{
    auto* panel = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    panel->SetBackgroundColour(BORDER);
    return panel;
}

wxString Sats(std::uint64_t value)
{
    return wxString::Format(value == 1 ? "%llu sat" : "%llu sats", static_cast<unsigned long long>(value));
}

void CopyText(const wxString& text)
{
    if (!text.empty() && wxTheClipboard->Open())
    {
        wxTheClipboard->SetData(new wxTextDataObject(text));
        wxTheClipboard->Close();
    }
}

class Sidebar final : public wxScrolledWindow
{
public:
    Sidebar(wxWindow* parent, std::function<void()> hide, NoticeCallback notice)
        : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(WIDTH, -1), wxVSCROLL),
          m_timer(this), m_notice(std::move(notice))
    {
        SetName("Multiplayer room panel");
        SetMinSize(wxSize(WIDTH, -1));
        SetMaxSize(wxSize(WIDTH, -1));
        SetBackgroundColour(BACKGROUND);
        SetForegroundColour(TEXT_COLOUR);
        SetScrollRate(0, 10);
        auto* root = new wxBoxSizer(wxVERTICAL);
        auto* head = new wxBoxSizer(wxHORIZONTAL);
        auto* title = Label(this, "MULTIPLAYER", 12, TEXT_COLOUR, true);
        title->SetFont(wxFont(12, wxFONTFAMILY_SWISS, wxFONTSTYLE_SLANT, wxFONTWEIGHT_BOLD));
        head->Add(title, 1, wxALIGN_CENTER_VERTICAL);
        auto* close = Button(this, "Hide ›", 8);
        close->SetName("Hide multiplayer panel");
        close->Bind(wxEVT_BUTTON, std::function<void(wxCommandEvent&)>([hide](wxCommandEvent&) { if (hide) hide(); }));
        head->Add(close, 0, wxALIGN_CENTER_VERTICAL);
        root->Add(head, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, INSET);
        m_phase = Label(this, "CREATE OR JOIN A ROOM", 8, MUTED);
        root->Add(m_phase, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM | wxEXPAND, INSET);
        root->Add(Divider(this), 0, wxEXPAND);

        auto* room = new wxBoxSizer(wxHORIZONTAL);
        m_code = Label(this, "No room", 11, TEXT_COLOUR, true);
        m_code->SetFont(wxFont(11, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        room->Add(m_code, 1, wxALIGN_CENTER_VERTICAL);
        m_copy = Button(this, "Copy invite", 8);
        m_copy->SetName("Copy room code");
        m_copy->Bind(wxEVT_BUTTON, std::function<void(wxCommandEvent&)>([this](wxCommandEvent&) { CopyText(m_room_code); }));
        room->Add(m_copy, 0);
        root->Add(room, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, INSET);
        m_room_details = Label(this, "", 8, MUTED);
        root->Add(m_room_details, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, INSET);
        m_ping = Label(this, "Peer ping —", 9, MUTED, true);
        m_ping->SetName("Peer gameplay ping");
        root->Add(m_ping, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, INSET);
        m_options = Button(this, "Room options", 8);
        m_options->SetName("Create room options");
        m_options->Bind(wxEVT_BUTTON, std::function<void(wxCommandEvent&)>([this](wxCommandEvent&) { RoomOptions(); }));
        root->Add(m_options, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, INSET);
        m_leave_room = Button(this, "Leave room", 8);
        m_leave_room->SetName("Leave room");
        m_leave_room->Bind(wxEVT_BUTTON, std::function<void(wxCommandEvent&)>([this](wxCommandEvent&) { LeaveRoom(); }));
        root->Add(m_leave_room, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, INSET);
        m_error = Label(this, "", 9, WARNING);
        m_error->SetName("Room payment notice");
        root->Add(m_error, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, INSET);
        m_next_match = Button(this, "Next match");
        m_next_match->SetName("Next room match");
        m_next_match->Bind(wxEVT_BUTTON, std::function<void(wxCommandEvent&)>([this](wxCommandEvent&) {
            const auto client = SatsArenaRoomClient::Active();
            if (client && client->NextMatch()) m_room_action_error.clear();
            else m_room_action_error = "Wait for the previous match to settle before starting the next entry.";
            Poll();
        }));
        root->Add(m_next_match, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, INSET);

        m_funding = Label(this, "", 9, GOLD);
        m_funding->SetName("Room funding progress");
        root->Add(m_funding, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, INSET);
        m_pay_entry = Button(this, "Pay your entry");
        m_pay_entry->SetBackgroundColour(GOLD);
        m_pay_entry->SetForegroundColour(BACKGROUND);
        m_pay_entry->SetName("Open your entry invoice");
        m_pay_entry->Bind(wxEVT_BUTTON, std::function<void(wxCommandEvent&)>([this](wxCommandEvent&) { OpenWallet(); }));
        root->Add(m_pay_entry, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, INSET);

        auto* summary = new wxPanel(this);
        summary->SetBackgroundColour(SURFACE);
        auto* totals = new wxBoxSizer(wxVERTICAL);
        totals->Add(Label(summary, "STILL IN PLAY", 8, MUTED), 0, wxBOTTOM, 3);
        auto* amounts = new wxBoxSizer(wxHORIZONTAL);
        m_remaining = Label(summary, "—", 17, TEXT_COLOUR, true);
        amounts->Add(m_remaining, 1, wxALIGN_CENTER_VERTICAL);
        m_paid_out = Label(summary, "", 8, GREEN);
        amounts->Add(m_paid_out, 0, wxALIGN_CENTER_VERTICAL);
        totals->Add(amounts, 0, wxEXPAND);
        summary->SetSizer(totals);
        auto* summary_border = new wxBoxSizer(wxVERTICAL);
        summary_border->Add(summary, 1, wxALL | wxEXPAND, INSET);
        auto* summary_panel = new wxPanel(this);
        summary->Reparent(summary_panel);
        summary_panel->SetBackgroundColour(SURFACE);
        summary_panel->SetSizer(summary_border);
        root->Add(summary_panel, 0, wxTOP | wxEXPAND, INSET);

        for (unsigned i = 0; i < 4; ++i)
        {
            auto& row = m_players[i];
            row.panel = new wxPanel(this);
            row.panel->SetBackgroundColour(BACKGROUND);
            auto* layout = new wxBoxSizer(wxVERTICAL);
            auto* names = new wxBoxSizer(wxHORIZONTAL);
            row.name = Label(row.panel, wxString::Format("P%u  Player %u", i + 1, i + 1),
                             9, PORTS[i], true, wxST_ELLIPSIZE_END);
            row.name->SetMinSize(wxSize(70, -1));
            names->Add(row.name, 1, wxALIGN_CENTER_VERTICAL);
            row.you = Label(row.panel, "YOU", 7, MUTED);
            names->Add(row.you, 0, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, 4);
            row.team = Label(row.panel, "", 8, MUTED);
            names->Add(row.team, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 4);
            row.entry = Label(row.panel, "", 8, GREEN);
            names->Add(row.entry, 0, wxALIGN_CENTER_VERTICAL);
            layout->Add(names, 0, wxEXPAND | wxBOTTOM, 3);
            auto* money = new wxBoxSizer(wxHORIZONTAL);
            auto* pot = new wxBoxSizer(wxVERTICAL);
            pot->Add(Label(row.panel, "IN POT", 7, MUTED), 0, wxBOTTOM, 2);
            row.pot = Label(row.panel, "—", 9);
            pot->Add(row.pot, 0);
            money->Add(pot, 1);
            auto* received = new wxBoxSizer(wxVERTICAL);
            received->Add(Label(row.panel, "RECEIVED", 7, MUTED), 0, wxBOTTOM, 2);
            row.received = Label(row.panel, "—", 9, GREEN);
            received->Add(row.received, 0);
            money->Add(received, 0);
            layout->Add(money, 0, wxLEFT | wxEXPAND, 19);
            layout->Add(Divider(row.panel), 0, wxTOP | wxEXPAND, 6);
            row.panel->SetSizer(layout);
            root->Add(row.panel, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, INSET);
        }
        root->AddStretchSpacer();
        m_terms = Label(this, "", 8, MUTED);
        m_terms->SetName("Room payment terms");
        root->Add(m_terms, 0, wxALL | wxEXPAND, INSET);
        m_wallet_button = Button(this, "Your wallet and entry");
        m_wallet_button->SetName("Your wallet and entry");
        m_wallet_button->Bind(wxEVT_BUTTON, std::function<void(wxCommandEvent&)>([this](wxCommandEvent&) { OpenWallet(); }));
        root->Add(m_wallet_button, 0, wxLEFT | wxRIGHT | wxEXPAND, INSET);
        auto* activity = Button(this, "Payment activity", 8);
        activity->Bind(wxEVT_BUTTON, std::function<void(wxCommandEvent&)>([this](wxCommandEvent&) { OpenActivity(); }));
        root->Add(activity, 0, wxALL | wxEXPAND, INSET);
        SetSizer(root);
        CreateWallet();
        Bind(wxEVT_TIMER, std::function<void(wxTimerEvent&)>([this](wxTimerEvent&) { Poll(); }));
        m_timer.Start(250);
        CallAfter(&Sidebar::Poll);
        NOTICE_LOG(SLIPPI_ONLINE, "[Arena] Room panel ready (create/join)");
    }

    ~Sidebar() override
    {
        m_timer.Stop();
        m_wallet->Destroy();
        if (m_activity) m_activity->Destroy();
    }

private:
    struct PlayerRow
    {
        wxPanel* panel = nullptr;
        wxStaticText *name = nullptr, *you = nullptr, *entry = nullptr, *team = nullptr;
        wxStaticText *pot = nullptr, *received = nullptr;
    };

    void RoomOptions()
    {
        const auto active = SatsArenaRoomClient::Active();
        if (active && !active->IsClosed())
        {
            SatsArenaNetplay::Room room;
            std::string error;
            if (active->Poll(room, error) != SatsArenaRoomClient::Status::Failed) return;
        }
        wxDialog dialog(this, wxID_ANY, "Create room options");
        dialog.SetBackgroundColour(BACKGROUND);
        auto* layout = new wxBoxSizer(wxVERTICAL);
        auto* count = new wxChoice(&dialog, wxID_ANY);
        for (const auto* value : {"2 players", "3 players", "4 players"}) count->Append(value);
        auto* mode = new wxChoice(&dialog, wxID_ANY);
        mode->Append("Free-for-all"); mode->Append("Teams");
        const auto current = SatsArenaRoomClient::CreateConfig();
        count->SetSelection(current.player_count - 2);
        mode->SetSelection(current.teams ? 1 : 0);
        layout->Add(Label(&dialog, "Choose before Create Room in Melee.", 10), 0, wxALL, INSET);
        layout->Add(count, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, INSET);
        layout->Add(mode, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, INSET);
        std::array<wxChoice*, 4> teams;
        for (unsigned i = 0; i < 4; ++i)
        {
            auto* row = new wxBoxSizer(wxHORIZONTAL);
            row->Add(Label(&dialog, wxString::Format("Player %u team", i + 1)), 1, wxALIGN_CENTER_VERTICAL);
            teams[i] = new wxChoice(&dialog, wxID_ANY);
            for (const auto* name : {"Red", "Blue", "Green"}) teams[i]->Append(name);
            teams[i]->SetSelection(current.team_ids[i] <= 2 ? current.team_ids[i] : i % 2);
            row->Add(teams[i], 0);
            layout->Add(row, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, INSET);
        }
        auto update = [&] {
            for (unsigned i = 0; i < 4; ++i)
                teams[i]->Enable(mode->GetSelection() == 1 && i < unsigned(count->GetSelection() + 2));
        };
        count->Bind(wxEVT_CHOICE, std::function<void(wxCommandEvent&)>([&](wxCommandEvent&) { update(); }));
        mode->Bind(wxEVT_CHOICE, std::function<void(wxCommandEvent&)>([&](wxCommandEvent&) { update(); }));
        auto* note = Label(&dialog, "Joining always uses the host's rules.", 9, MUTED);
        note->Wrap(300);
        layout->Add(note, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, INSET);
        layout->Add(dialog.CreateButtonSizer(wxOK | wxCANCEL), 0, wxALL | wxEXPAND, INSET);
        dialog.Bind(wxEVT_BUTTON, std::function<void(wxCommandEvent&)>([&](wxCommandEvent&) {
            SatsArenaNetplay::Config config;
            config.player_count = count->GetSelection() + 2;
            config.teams = mode->GetSelection() == 1;
            for (unsigned i = 0; i < config.player_count; ++i)
                if (config.teams) config.team_ids[i] = teams[i]->GetSelection();
            if (SatsArenaRoomClient::SetCreateConfig(config)) dialog.EndModal(wxID_OK);
            else {
                note->SetLabel("Use at least two team colors. Options lock while in a room.");
                note->SetForegroundColour(WARNING);
                note->Wrap(300);
                dialog.GetSizer()->Fit(&dialog);
            }
        }), wxID_OK);
        update();
        dialog.SetSizerAndFit(layout);
        dialog.CentreOnParent();
        dialog.ShowModal();
        Poll();
    }

    void CreateWallet()
    {
        m_wallet = new wxDialog(this, wxID_ANY, "Your wallet and entry", wxDefaultPosition,
                                wxDefaultSize, wxDEFAULT_DIALOG_STYLE);
        m_wallet->SetBackgroundColour(BACKGROUND);
        auto* layout = new wxBoxSizer(wxVERTICAL);
        m_wallet_title = Label(m_wallet, "Your entry", 14, TEXT_COLOUR, true);
        layout->Add(m_wallet_title, 0, wxALL, INSET);
        m_wallet_terms = Label(m_wallet, "", 10, MUTED);
        layout->Add(m_wallet_terms, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, INSET);
        layout->Add(Label(m_wallet, "Receiving address", 9), 0, wxLEFT | wxRIGHT | wxBOTTOM, INSET);
        m_address = new wxTextCtrl(m_wallet, wxID_ANY);
        m_address->SetName("Your room receiving address");
        m_address->SetMaxLength(127);
        m_address->SetMinSize(wxSize(310, -1));
        m_address->SetHint("Your Lightning address");
        m_address->Bind(wxEVT_TEXT, std::function<void(wxCommandEvent&)>([this](wxCommandEvent&) {
            m_dirty = true;
            m_edited = std::chrono::steady_clock::now();
            m_attempted.clear();
            m_address_error.clear();
            SetInvoice("");
        }));
        layout->Add(m_address, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, INSET);
        m_verification = Label(m_wallet, "", 9, MUTED);
        layout->Add(m_verification, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, INSET);
        m_retry = Button(m_wallet, "Retry receiving check", 9);
        m_retry->Bind(wxEVT_BUTTON, std::function<void(wxCommandEvent&)>([this](wxCommandEvent&) {
            m_attempted.clear(); m_address_error.clear();
        }));
        layout->Add(m_retry, 0, wxLEFT | wxRIGHT | wxBOTTOM, INSET);
        auto* invoice = new wxBoxSizer(wxHORIZONTAL);
        m_qr = new wxStaticBitmap(m_wallet, wxID_ANY, wxBitmap(1, 1));
        m_qr->SetName("Your entry payment QR code");
        invoice->Add(m_qr, 0, wxRIGHT, INSET);
        auto* details = new wxBoxSizer(wxVERTICAL);
        m_invoice_amount = Label(m_wallet, "", 14, GOLD, true);
        m_invoice_amount->SetMinSize(wxSize(140, -1));
        details->Add(m_invoice_amount, 0, wxBOTTOM, 6);
        auto* own = Label(m_wallet, "Only your own entry invoice appears here.", 9, MUTED);
        own->Wrap(140);
        details->Add(own, 0, wxBOTTOM, 8);
        m_copy_invoice = Button(m_wallet, "Copy invoice", 9);
        m_copy_invoice->Bind(wxEVT_BUTTON, std::function<void(wxCommandEvent&)>([this](wxCommandEvent&) { CopyText(m_invoice); }));
        details->Add(m_copy_invoice, 0);
        invoice->Add(details, 1);
        m_invoice_box = invoice;
        layout->Add(invoice, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, INSET);
        m_wallet_status = Label(m_wallet, "", 10, GREEN);
        layout->Add(m_wallet_status, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, INSET);
        auto* done = Button(m_wallet, "Done");
        done->Bind(wxEVT_BUTTON, std::function<void(wxCommandEvent&)>([this](wxCommandEvent&) { m_wallet->Hide(); }));
        layout->Add(done, 0, wxALL | wxEXPAND, INSET);
        m_wallet->SetSizerAndFit(layout);
        m_wallet->Bind(wxEVT_CLOSE_WINDOW, std::function<void(wxCloseEvent&)>([this](wxCloseEvent& event) {
            if (!event.CanVeto()) { event.Skip(); return; }
            m_wallet->Hide(); event.Veto();
        }));
    }

    void OpenWallet()
    {
        Poll();
        m_wallet->Fit();
        if (!m_wallet->IsShown()) m_wallet->CentreOnParent();
        m_wallet->Show();
        m_wallet->Raise();
    }

    void OpenActivity()
    {
        if (!m_activity)
        {
            m_activity = new wxDialog(this, wxID_ANY, "Room payment activity", wxDefaultPosition,
                                     wxSize(520, 300), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
            auto* layout = new wxBoxSizer(wxVERTICAL);
            m_activity_text = new wxTextCtrl(m_activity, wxID_ANY, "", wxDefaultPosition,
                                           wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
            layout->Add(m_activity_text, 1, wxALL | wxEXPAND, INSET);
            m_activity->SetSizer(layout);
            m_activity->Bind(wxEVT_CLOSE_WINDOW, std::function<void(wxCloseEvent&)>([this](wxCloseEvent& event) {
                if (!event.CanVeto()) { event.Skip(); return; }
                m_activity->Hide(); event.Veto();
            }));
        }
        m_activity_text->ChangeValue(m_activity_log);
        m_activity->Show();
        m_activity->Raise();
    }

    void LeaveRoom()
    {
        const auto client = SatsArenaRoomClient::Active();
        if (!client || client->IsClosed()) return;
        const bool host = client->LocalSlot() == 0;
        ArenaLocalView view = {};
        const bool playing = SatsArenaLocal::RoomView(client->PaymentView(), view) && view.phase == 3;
        if (host || playing)
        {
            const wxString question = host ? "Close this room for everyone?" : "Leave this match and room?";
            wxMessageDialog dialog(this, question +
                "\n\nPending payments stay on the server for settlement. The game window stays open.",
                host ? "Close room" : "Leave room", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
            if (dialog.ShowModal() != wxID_YES) return;
        }
        if (host ? client->CloseRoom() : client->LeaveRoom()) m_room_action_error.clear();
        else m_room_action_error = "The room action could not be queued. Try again when connected.";
        Poll();
    }

    void SetInvoice(const wxString& invoice)
    {
        if (m_invoice != invoice)
        {
            m_invoice = invoice;
            m_qr->SetBitmap(invoice.empty() ? wxBitmap(1, 1) :
                SatsArenaWidgets::InvoiceQR(invoice.utf8_str().data()));
        }
        m_invoice_box->ShowItems(!invoice.empty());
        m_copy_invoice->Enable(!invoice.empty());
    }

    void ResetAddress()
    {
        m_address->ChangeValue("");
        m_dirty = m_pending = false;
        m_submitted.clear(); m_attempted.clear(); m_address_error.clear();
        SetInvoice("");
    }

    void Address(const std::shared_ptr<SatsArenaRoomClient>& client, const ArenaLocalView& view)
    {
        const auto now = std::chrono::steady_clock::now();
        const auto address = wxString::FromUTF8(view.addresses[m_slot]);
        if (!m_dirty && !m_pending && m_address->GetValue() != address) m_address->ChangeValue(address);
        if (m_pending)
        {
            if (m_address->GetValue() != m_submitted) { m_pending = false; m_attempted.clear(); }
            else if (address == m_submitted)
            {
                m_dirty = false;
                if (view.verification_state[m_slot] != 0 && view.verification_state[m_slot] != 1 &&
                    now - m_submitted_at >= std::chrono::seconds(1)) m_pending = false;
            }
            if (m_pending && ((view.error[0] && wxString::FromUTF8(view.error) != m_error_at_submit) ||
                now - m_submitted_at > std::chrono::seconds(45)))
            {
                m_pending = false;
                m_address_error = view.error[0] ? wxString::FromUTF8(view.error) :
                    wxString("Server did not confirm the address. Retry when connected.");
            }
        }
        if (!m_dirty && !m_pending)
        {
            if (view.verification_state[m_slot] == 2) m_address_error.clear();
            else if (view.verification_state[m_slot] == 3 || view.verification_state[m_slot] == 5)
                m_address_error = wxString::FromUTF8(view.verification_message[m_slot]);
        }
        const bool editable = (view.phase == 1 || view.phase == 2) && !view.paid_sats[m_slot];
        const auto input = m_address->GetValue();
        if (editable && !m_pending && !input.empty() && input != m_attempted &&
            (m_dirty || view.verification_state[m_slot] != 2) &&
            now - m_edited >= std::chrono::milliseconds(750))
        {
            m_attempted = input;
            if (client->SetAddress(input.utf8_str().data()))
            {
                m_pending = true; m_submitted = input; m_submitted_at = now;
                m_address_error.clear(); m_error_at_submit = wxString::FromUTF8(view.error);
            }
            else m_address_error = "Address could not be queued. Check the room connection.";
        }
        m_address->Enable(editable);
        m_retry->Show(editable && !m_address_error.empty());
        m_retry->Enable(!m_pending);
        const bool verified = !m_dirty && !m_pending && view.verification_state[m_slot] == 2;
        wxString message = m_address_error;
        if (message.empty()) message = m_pending ? "Checking receiver..." :
            (verified ? "Receiving address verified" : "Enter your receiving address for an entry invoice.");
        m_verification->SetLabel(message);
        m_verification->SetForegroundColour(!m_address_error.empty() ? WARNING : (verified ? GREEN : MUTED));
        m_verification->Wrap(310);
        SetInvoice(editable && verified && !view.error[0] ? wxString::FromUTF8(view.invoices[m_slot]) : wxString());
    }

    void Poll()
    {
        const auto client = SatsArenaRoomClient::Active();
        if (client != m_client.lock() || (!client && m_had_client))
        {
            m_lost_funds = !client && m_had_funds;
            ResetAddress();
            m_match_id.clear();
            m_room_action_error.clear();
            m_client = client;
        }
        m_had_client = bool(client);
        m_slot = client ? client->LocalSlot() : -1;
        auto config = client ? client->GetConfig() : SatsArenaRoomClient::CreateConfig();
        SatsArenaNetplay::Room room;
        std::string error;
        const auto state = client ? client->Poll(room, error) : SatsArenaRoomClient::Status::Waiting;
        const bool closed = client && client->IsClosed();
        const bool connected = client && !closed && state != SatsArenaRoomClient::Status::Failed;
        m_room_code = client ? wxString::FromUTF8(client->GetRoomCode().c_str()) : wxString();
        m_code->SetLabel(m_room_code.empty() ? "No room" : m_room_code);
        m_copy->Enable(connected && !m_room_code.empty());
        m_options->Show(!connected);
        m_options->Enable(!connected);
        m_room_details->SetLabel(wxString::Format("%s · %u PLAYERS", config.teams ? "TEAMS" : "FFA", config.player_count));
        const auto pings = client ? client->PeerPings() : SatsArenaRoomClient::PingSnapshot{};
        const auto ping = pings.WorstMilliseconds(m_slot, config.player_count);
        m_ping->SetLabel(ping ? wxString::Format("Peer ping %u ms%s", ping, ping >= 150 ? " · HIGH" : "") :
                               wxString("Peer ping —"));
        m_ping->SetForegroundColour(ping >= 200 ? PORTS[0] : ping >= 100 ? WARNING : ping ? GREEN : MUTED);
        wxString ping_details = "Gameplay round-trip time to players, not the room server.\nHighest recent peer ping shown; samples expire after 3 seconds.";
        for (unsigned slot = 0; slot < config.player_count; ++slot)
            if (static_cast<int>(slot) != m_slot)
            {
                const auto ms = pings.players[slot].Milliseconds();
                ping_details += ms ? wxString::Format("\nP%u: %u ms", slot + 1, ms) :
                                     wxString::Format("\nP%u: —", slot + 1);
            }
        m_ping->SetToolTip(ping_details);
        m_ping->Show(connected && pings.active);
        ArenaLocalView view = {};
        const auto payment = client ? client->PaymentView() : nlohmann::json(nullptr);
        const bool valid = client && state != SatsArenaRoomClient::Status::Failed && !payment.is_null() &&
            m_slot >= 0 && m_slot < int(config.player_count) && SatsArenaLocal::RoomView(payment, view);
        const bool free_room = connected && state != SatsArenaRoomClient::Status::Failed && payment.is_null() && m_slot >= 0;
        const auto match_id = valid ? payment["match_id"].get<std::string>() : std::string();
        if (match_id != m_match_id)
        {
            ResetAddress();
            m_room_action_error.clear();
            m_match_id = match_id;
        }
        wxString problem;
        if (client && !closed && (state == SatsArenaRoomClient::Status::Failed || !error.empty()))
            problem = error.empty() ? wxString("Room disconnected. Check settlement status with the server.") : wxString::FromUTF8(error.c_str());
        else if (client && !payment.is_null() && !valid) problem = "Invalid payment state. No invoice can be displayed.";
        else if (valid && view.error[0]) problem = wxString::FromUTF8(view.error);
        else if (!client && m_lost_funds) problem = "Room closed. Check server settlement for remaining funds.";
        else problem = m_room_action_error;
        const bool funding = valid && !closed && (view.phase == 1 || view.phase == 2);
        unsigned paid = 0;
        std::uint64_t remaining = 0, received = 0, pending = 0;
        for (unsigned i = 0; i < config.player_count; ++i)
        {
            paid += view.paid_sats[i] != 0;
            remaining += view.remaining_sats[i]; received += view.earned_sats[i]; pending += view.pending_sats[i];
        }
        if (valid) m_had_funds = view.phase != 4 && remaining + pending != 0;
        wxString phase, compact;
        if (!problem.empty() || (valid && view.phase == 7)) phase = compact = valid ? "Payments paused" : "Room connection interrupted";
        else if (closed) { phase = compact = m_slot == 0 ? "Room closed" : "Left room"; }
        else if (!client) { phase = "Create or join a room in Melee"; compact = "No room"; }
        else if (free_room && state == SatsArenaRoomClient::Status::Waiting)
        {
            const auto joined = SatsArenaNetplay::JoinedPlayers(room);
            phase = joined < config.player_count ?
                wxString::Format("Waiting for players · %u / %u joined", joined, config.player_count) :
                wxString("All players joined · preparing match");
            compact = wxString::Format("%u / %u joined", joined, config.player_count);
        }
        else if (!valid) { phase = m_slot < 0 ? "Connecting to room..." : "Free room · no entry payment"; compact = m_slot < 0 ? "Connecting" : "Free room"; }
        else if (view.phase == 3) phase = compact = "Match in progress";
        else if (view.phase == 6) phase = compact = "Settling payments";
        else if (view.phase == 4) { phase = m_slot == 0 ? "Match settled · next match ready" : "Match settled · waiting for host"; compact = "Match settled"; }
        else { phase = view.phase == 2 ? "Ready to play" : "Waiting for entries"; compact = wxString::Format("%u / %u paid", paid, config.player_count); }
        const bool attention = !problem.empty() || (valid && view.phase == 7);
        m_phase->SetLabel(phase.Upper()); m_phase->Wrap(WIDTH - INSET * 2);
        m_phase->SetForegroundColour(attention ? WARNING : (client ? GREEN : MUTED));
        m_error->SetLabel(problem); m_error->Wrap(WIDTH - INSET * 2); m_error->Show(!problem.empty());
        m_funding->SetLabel(wxString::Format("%u of %u entries paid", paid, config.player_count));
        m_funding->Show(funding);
        const bool can_next = connected && m_slot == 0 && client->CanNextMatch();
        m_next_match->Show(connected && m_slot == 0 && (can_next || (valid && view.phase == 4)));
        m_next_match->Enable(can_next);
        m_leave_room->SetLabel(m_slot == 0 ? "Close room" : "Leave room");
        m_leave_room->SetName(m_slot == 0 ? "Close room" : "Leave room");
        m_leave_room->Show(connected);
        const bool own_unpaid = funding && !view.paid_sats[m_slot];
        m_pay_entry->SetLabel(valid ? "Pay your " + Sats(view.invoice_sats) + " entry" : "Pay your entry");
        m_pay_entry->Show(own_unpaid); m_pay_entry->Enable(own_unpaid && problem.empty());
        m_remaining->SetLabel(valid ? Sats(remaining) : (free_room ? wxString("Free room") : wxString("—")));
        m_paid_out->SetLabel(valid ? Sats(received) + " paid out" : wxString());
        for (unsigned i = 0; i < 4; ++i)
        {
            auto& row = m_players[i];
            row.panel->Show(i < config.player_count);
            wxString name = wxString::Format("Player %u", i + 1);
            if (i < room.players.size() && !room.players[i].name.empty()) name = wxString::FromUTF8(room.players[i].name.c_str());
            else if (int(i) == m_slot) { const char* own = std::getenv("SATS_ARENA_ROOM_NAME"); if (own && *own) name = wxString::FromUTF8(own); }
            row.name->SetLabel(wxString::Format("P%u  %s", i + 1, name)); row.name->SetToolTip(name);
            const unsigned team = config.team_ids[i];
            row.name->SetForegroundColour(config.teams && team < 3 ? TEAMS[team] : PORTS[i]);
            row.you->Show(int(i) == m_slot);
            row.team->SetLabel(config.teams && team < 3 ? wxString(std::array<const char*, 3>{{"Red", "Blue", "Green"}}[team]) : wxString());
            row.team->Show(config.teams);
            row.entry->SetLabel(valid ? (view.paid_sats[i] ? "✓ Paid" : "Unpaid") : (free_room ? "No entry" : ""));
            row.entry->SetForegroundColour(valid && !view.paid_sats[i] ? GOLD : GREEN);
            row.pot->SetLabel(valid ? Sats(view.remaining_sats[i]) : wxString("—"));
            row.received->SetLabel(valid ? Sats(view.earned_sats[i]) : wxString("—"));
            row.received->SetToolTip(valid && view.pending_sats[i] ? Sats(view.pending_sats[i]) + " pending" : wxString());
            row.panel->Layout();
        }
        m_terms->SetLabel(valid ? Sats(view.entry_sats) + " / player · " + Sats(view.sats_per_percent) + " / 1%\n" +
            (view.sandbox ? wxString("Sandbox · ") : (view.simulated ? wxString("Simulated · ") : wxString())) +
            Sats(view.surcharge_sats) + " fees · " + wxString::Format("%llus batches", static_cast<unsigned long long>(view.batch_window_ms / 1000)) :
            (free_room ? wxString("No entry payment for this room.") :
                (client ? wxString("Payment details unavailable.") : wxString("Room options are fixed after joining."))));
        m_terms->Wrap(WIDTH - INSET * 2);
        m_wallet_button->Enable(connected || valid);
        m_wallet_button->SetLabel(valid && view.paid_sats[m_slot] ? "Your wallet and receipt" : "Your wallet and entry");
        m_wallet_title->SetLabel(m_slot >= 0 ? wxString::Format("Your wallet · Player %d", m_slot + 1) : wxString("Your wallet"));
        m_wallet_terms->SetLabel(valid ?
            (view.sandbox ? wxString("SANDBOX · test sats\n") : (view.simulated ? wxString("SIMULATED · test only\n") : wxString())) + Sats(view.entry_sats) + " into play + " + Sats(view.surcharge_sats) + " fees\n" +
            Sats(view.sats_per_percent) + " / 1% damage" : (problem.empty() ?
                (free_room ? wxString("No entry payment for this room.") : wxString("Waiting for room payment details.")) : problem));
        m_wallet_terms->Wrap(310);
        m_invoice_amount->SetLabel(valid ? Sats(view.invoice_sats) : wxString());
        if (valid && !closed) Address(client, view);
        else { ResetAddress(); m_address->Enable(false); m_retry->Hide(); m_verification->SetLabel(""); }
        m_wallet_status->SetLabel(!problem.empty() ? problem : (valid && view.paid_sats[m_slot] ?
            "Your entry is confirmed.\n" + Sats(view.remaining_sats[m_slot]) + " in pot · " + Sats(view.earned_sats[m_slot]) + " received" :
            (valid ? wxString("Pay with your own wallet after receiving verification.") : wxString("This room has no payable invoice."))));
        m_wallet_status->SetForegroundColour(attention ? WARNING : GREEN); m_wallet_status->Wrap(310);
        m_wallet->Layout();
        if (m_wallet->IsShown()) m_wallet->Fit();
        m_activity_log = valid && view.payment_log[0] ? wxString::FromUTF8(view.payment_log) : wxString("No payment activity yet.");
        if (m_activity_text && m_activity_text->GetStringSelection().empty() && m_activity_text->GetValue() != m_activity_log)
            m_activity_text->ChangeValue(m_activity_log);
        Layout(); FitInside();
        if (m_notice && (compact != m_last_notice || attention != m_last_attention))
        {
            m_last_notice = compact; m_last_attention = attention; m_notice(compact, attention);
        }
    }

    wxTimer m_timer;
    NoticeCallback m_notice;
    std::weak_ptr<SatsArenaRoomClient> m_client;
    int m_slot = -1;
    bool m_had_client = false, m_had_funds = false, m_lost_funds = false;
    bool m_dirty = false, m_pending = false, m_last_attention = false;
    wxString m_room_code, m_invoice, m_submitted, m_attempted, m_address_error, m_error_at_submit;
    wxString m_last_notice, m_activity_log;
    wxString m_room_action_error;
    std::string m_match_id;
    std::chrono::steady_clock::time_point m_edited = {}, m_submitted_at = {};
    std::array<PlayerRow, 4> m_players;
    wxStaticText *m_phase = nullptr, *m_code = nullptr, *m_room_details = nullptr, *m_ping = nullptr, *m_error = nullptr;
    wxStaticText *m_funding = nullptr, *m_remaining = nullptr, *m_paid_out = nullptr, *m_terms = nullptr;
    wxButton *m_copy = nullptr, *m_options = nullptr, *m_pay_entry = nullptr, *m_wallet_button = nullptr;
    wxButton *m_next_match = nullptr, *m_leave_room = nullptr;
    wxDialog *m_wallet = nullptr, *m_activity = nullptr;
    wxStaticText *m_wallet_title = nullptr, *m_wallet_terms = nullptr, *m_verification = nullptr;
    wxStaticText *m_invoice_amount = nullptr, *m_wallet_status = nullptr;
    wxTextCtrl *m_address = nullptr, *m_activity_text = nullptr;
    wxStaticBitmap* m_qr = nullptr;
    wxBoxSizer* m_invoice_box = nullptr;
    wxButton *m_retry = nullptr, *m_copy_invoice = nullptr;
};
}

wxWindow* CreateSidebar(wxWindow* parent, std::function<void()> hide, NoticeCallback notice)
{
    return new Sidebar(parent, std::move(hide), std::move(notice));
}
}
