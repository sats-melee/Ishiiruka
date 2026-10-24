// Copyright 2026 Sats Arena contributors. Licensed under GPLv2+.
#include "DolphinWX/SatsArenaLocal.h"

#include <cstdlib>
#include <cstring>
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/toplevel.h>

#include "Common/FileUtil.h"
#include "Core/Core.h"
#include "sats_arena_local.h"
static_assert(sizeof(ArenaLocalView) == 576, "Arena local ABI layout mismatch");

namespace SatsArenaLocal
{
namespace
{
bool s_enabled = false;

struct Host
{
	ArenaLocalHandle* handle = nullptr;
	~Host() { arena_local_destroy(handle); }
	ArenaLocalHandle* Get()
	{
		if (!handle)
		{
			const char* override_path = std::getenv("SATS_ARENA_FAKE_DATA_DIR");
			const std::string folder = override_path && *override_path ? override_path :
			    File::GetUserPath(D_USER_IDX) + "SatsArenaFake";
			handle = arena_local_create(folder.c_str());
		}
		return handle;
	}
};

Host& GetHost() { static Host host; return host; }

wxString Phase(unsigned phase)
{
	switch (phase)
	{
	case 0: return "Opening local engine...";
	case 1: return "Funding: both players must pay and reserve their budgets.";
	case 2: return "Ready: both 500-sat budgets are reserved.";
	case 3: return "Session started. Finish it below after stopping emulation.";
	case 4: return "Settled. Unused fake funds are available for the next lobby.";
	case 5: return "Cancelled. Reserved funds returned to player balances.";
	case 6: return "Disputed: funds remain reserved for review.";
	default: return "Local engine error.";
	}
}

class FundingDialog final : public wxDialog
{
public:
	FundingDialog(wxWindow* parent, bool for_boot) : wxDialog(wxGetTopLevelParent(parent), wxID_ANY, "Sats Arena - FAKE local funding",
	    wxDefaultPosition, wxSize(660, 700), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER), m_timer(this), m_for_boot(for_boot)
	{
		m_handle = GetHost().Get();
		auto* root = new wxBoxSizer(wxVERTICAL);
		auto* notice = new wxStaticText(this, wxID_ANY,
		    "FAKE WALLET ONLY - no real sats, no NWC connection.\n"
		    "500 sats maximum loss per player; 100-sat win bonus; damage billing OFF.\n"
		    "This gates emulator launch, not every in-game match. Results below are manual test inputs.");
		notice->Wrap(620);
		root->Add(notice, 0, wxALL | wxEXPAND, 12);
		m_phase = new wxStaticText(this, wxID_ANY, "Opening local engine...");
		root->Add(m_phase, 0, wxALL | wxEXPAND, 12);
		for (int i = 0; i < 2; ++i)
		{
			m_player[i] = new wxStaticText(this, wxID_ANY, wxString::Format("Player %d: waiting...", i + 1));
			root->Add(m_player[i], 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, 12);
			m_invoice[i] = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
			m_invoice[i]->SetName(wxString::Format("Player %d fake invoice", i + 1));
			root->Add(m_invoice[i], 0, wxALL | wxEXPAND, 12);
			m_pay[i] = new wxButton(this, wxID_ANY, wxString::Format("Simulate Player %d payment", i + 1));
			m_pay[i]->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) { Send(2 + i); });
			root->Add(m_pay[i], 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
		}
		m_error = new wxStaticText(this, wxID_ANY, wxEmptyString);
		m_error->SetForegroundColour(wxColour(220, 90, 65));
		root->Add(m_error, 0, wxALL | wxEXPAND, 12);
		auto* actions = new wxBoxSizer(wxHORIZONTAL);
		m_start = new wxButton(this, wxID_ANY, for_boot ? "Start funded session" : "Return to Play");
		m_start->Enable(false);
		m_start->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			if (!m_for_boot) { EndModal(wxID_CANCEL); return; }
			m_starting = Send(4);
		});
		actions->Add(m_start, 0, wxRIGHT, 8);
		m_cancel = new wxButton(this, wxID_ANY, "Cancel funding");
		m_cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Send(5); });
		actions->Add(m_cancel, 0, wxRIGHT, 8);
		m_new = new wxButton(this, wxID_ANY, "New lobby");
		m_new->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Send(1); });
		actions->Add(m_new);
		root->Add(actions, 0, wxALL, 12);
		root->Add(new wxStaticText(this, wxID_ANY, "Manual fake result (not inferred from the game):"), 0, wxLEFT | wxTOP, 12);
		auto* results = new wxBoxSizer(wxHORIZONTAL);
		const char* labels[] = { "Player 1 wins", "Player 2 wins", "No contest" };
		for (int i = 0; i < 3; ++i)
		{
			m_finish[i] = new wxButton(this, wxID_ANY, labels[i]);
			m_finish[i]->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) { Send(6 + i); });
			results->Add(m_finish[i], 0, wxRIGHT, 8);
		}
		root->Add(results, 0, wxALL, 12);
		auto* close = new wxButton(this, wxID_CANCEL, "Keep funds and close");
		root->Add(close, 0, wxALL | wxALIGN_RIGHT, 12);
		SetSizer(root);
		SetMinSize(wxSize(650, 680));
		CentreOnParent();
		Bind(wxEVT_SHOW, [this](wxShowEvent& event) {
			if (event.IsShown())
			{
				Raise();
				m_start->SetFocus();
			}
			event.Skip();
		});
		Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Poll(); });
		m_timer.Start(100);
		Poll();
	}

	~FundingDialog() override { m_timer.Stop(); }

private:
	bool Send(unsigned action)
	{
		if (!m_handle || !arena_local_action(m_handle, action))
		{
			m_error->SetLabel("Engine is busy. Please try again.");
			return false;
		}
		m_start->Enable(false);
		return true;
	}

	void Poll()
	{
		ArenaLocalView view = {};
		if (!m_handle) { m_error->SetLabel("Could not create the local fake engine."); return; }
		if (!arena_local_snapshot(m_handle, &view)) return;
		if (view.abi_version != 1) { m_error->SetLabel("Local engine ABI mismatch."); return; }
		m_phase->SetLabel(Phase(view.phase));
		for (int i = 0; i < 2; ++i)
		{
			m_player[i]->SetLabel(wxString::Format("Player %d: %llu available / %llu reserved sats", i + 1,
			    static_cast<unsigned long long>(view.available_sats[i]), static_cast<unsigned long long>(view.reserved_sats[i])));
			m_invoice[i]->ChangeValue(view.invoices[i][0] ? wxString::FromUTF8(view.invoices[i]) : "No payment needed");
			m_pay[i]->Enable(!view.busy && view.phase == 1 && view.reserved_sats[i] == 0);
		}
		m_start->Enable(!view.busy && view.phase == 2 && !m_starting);
		m_cancel->Enable(!view.busy && (view.phase == 1 || view.phase == 2));
		m_new->Enable(!view.busy && (view.phase == 4 || view.phase == 5));
		for (auto* button : m_finish) button->Enable(!view.busy && view.phase == 3 && !Core::IsRunning());
		m_error->SetLabel(wxString::FromUTF8(view.error));
		m_error->Wrap(620);
		if (m_starting && !view.busy && (view.phase == 3 || view.error[0]))
		{
			m_starting = false;
			if (view.phase == 3 && !view.error[0]) EndModal(wxID_OK);
		}
		Layout();
	}

	ArenaLocalHandle* m_handle = nullptr;
	wxTimer m_timer;
	bool m_for_boot = false;
	bool m_starting = false;
	wxStaticText* m_phase = nullptr;
	wxStaticText* m_error = nullptr;
	wxStaticText* m_player[2] = {};
	wxTextCtrl* m_invoice[2] = {};
	wxButton* m_pay[2] = {};
	wxButton* m_start = nullptr;
	wxButton* m_cancel = nullptr;
	wxButton* m_new = nullptr;
	wxButton* m_finish[3] = {};
};
}

bool AllowBoot(wxWindow* parent, const std::string&)
{
	const char* env = std::getenv("SATS_ARENA_FAKE_LOCAL");
	if (!s_enabled && (!env || std::strcmp(env, "1") != 0)) return true;
	s_enabled = true;
	FundingDialog dialog(parent, true);
	return dialog.ShowModal() == wxID_OK;
}

void ShowFunding(wxWindow* parent)
{
	if (Core::IsRunning())
	{
		wxMessageBox("Stop emulation before preparing a fake funded session.", "Sats Arena", wxOK | wxICON_INFORMATION, parent);
		return;
	}
	s_enabled = true;
	FundingDialog dialog(parent, false);
	dialog.ShowModal();
}
}
