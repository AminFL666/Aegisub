// Copyright (c) 2026, Aegisub Original Subtitle contributors
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright
//     notice, this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright
//     notice, this list of conditions and the following disclaimer in the
//     documentation and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its
//     contributors may be used to endorse or promote products derived
//     from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Aegisub Project http://www.aegisub.org/

/// @file original_grid.cpp
/// @brief The original subtitle pane shown on the right of the main window
/// @ingroup main_ui

#include "original_grid.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "base_grid.h"
#include "compat.h"
#include "include/aegisub/context.h"
#include "options.h"
#include "selection_controller.h"
#include "subs_edit_box.h"
#include "subs_original.h"
#include "utils.h"

#include <libaegisub/util.h>

#include <algorithm>

#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/intl.h>
#include <wx/settings.h>

OriginalGrid::OriginalGrid(wxWindow *parent, agi::Context *context)
: wxWindow(parent, -1, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS | wxSUNKEN_BORDER)
, context(context)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);

	UpdateStyle();

	connections = agi::signal::make_vector({
		context->selectionController->AddActiveLineListener([this] { OnActiveLineChanged(); }),
		context->selectionController->AddSelectionListener([this] { Refresh(false); }),
		context->originalSubs->AddChangeListener([this] { Refresh(false); }),
		context->originalSubs->AddOpenListener([this] { Refresh(false); }),
		OPT_SUB("Colour/Subtitle Grid/Background/Background", [this] { Refresh(false); }),
		OPT_SUB("Colour/Subtitle Grid/Header", [this] { Refresh(false); }),
		OPT_SUB("Subtitle/Grid/Font Face", &OriginalGrid::UpdateStyle, this),
		OPT_SUB("Subtitle/Grid/Font Size", &OriginalGrid::UpdateStyle, this),
	});

	Bind(wxEVT_PAINT, &OriginalGrid::OnPaint, this);
	Bind(wxEVT_SIZE, &OriginalGrid::OnSize, this);
	Bind(wxEVT_LEFT_DOWN, &OriginalGrid::OnMouse, this);
	Bind(wxEVT_LEFT_DCLICK, &OriginalGrid::OnMouse, this);
	Bind(wxEVT_MOUSEWHEEL, &OriginalGrid::OnMouseWheel, this);
	Bind(wxEVT_KEY_DOWN, &OriginalGrid::OnKeyDown, this);
	Bind(wxEVT_DPI_CHANGED, &OriginalGrid::OnDPIChanged, this);
	Bind(wxEVT_SHOW, &OriginalGrid::OnShow, this);
}

OriginalGrid::~OriginalGrid() = default;

void OriginalGrid::UpdateStyle() {
	font = *wxNORMAL_FONT;
	wxString fontname = FontFace("Subtitle/Grid");
	if (!fontname.empty())
		font.SetFaceName(fontname);
	font.SetPointSize(OPT_GET("Subtitle/Grid/Font Size")->GetInt());

	wxClientDC dc(this);
	dc.SetFont(font);
	lineHeight = dc.GetCharHeight() + 4;

	Refresh(false);
}

void OriginalGrid::SyncScroll(int y) {
	if (y == yPos)
		return;

	yPos = y;
	Refresh(false);
}

void OriginalGrid::MakeRowVisible(int row) {
	// The pane has no scrollbar of its own, so scrolling it means scrolling
	// the subtitle grid it is lined up with
	if (!context->subsGrid)
		return;

	int h = GetClientSize().GetHeight();
	if (row < yPos + 1)
		context->subsGrid->ScrollTo(row - 1);
	else if (row > yPos + h / lineHeight - 3)
		context->subsGrid->ScrollTo(row - h / lineHeight + 3);
}

int OriginalGrid::RowAtPoint(int y) const {
	return y / lineHeight + yPos - 1;
}

void OriginalGrid::OnSize(wxSizeEvent &) {
	Refresh(false);
}

void OriginalGrid::OnShow(wxShowEvent &event) {
	event.Skip();

	// The pane is only scrolled when the grid scrolls, so pick the grid's
	// position up again when the pane is shown after having been hidden
	if (event.IsShown() && context->subsGrid)
		SyncScroll(context->subsGrid->GetScrollPosition());
}

void OriginalGrid::OnDPIChanged(wxDPIChangedEvent &event) {
	event.Skip();
	UpdateStyle();
}

void OriginalGrid::OnActiveLineChanged() {
	if (!context->originalSubs || !context->originalSubs->IsOpen())
		return;

	auto *active = context->selectionController->GetActiveLine();
	if (active)
		MakeRowVisible(active->Row);
	Refresh(false);
}

void OriginalGrid::OnKeyDown(wxKeyEvent &event) {
	if (!context->originalSubs || !context->originalSubs->IsOpen()) {
		event.Skip();
		return;
	}

	switch (event.GetKeyCode()) {
		case WXK_UP:
			context->selectionController->PrevLine();
			break;
		case WXK_DOWN:
			context->selectionController->NextLine();
			break;
		case WXK_RETURN:
			if (context->subsEditBox)
				context->subsEditBox->SetEditingOriginal(true);
			break;
		default:
			event.Skip();
	}
}

void OriginalGrid::SelectPairedLine(int row, bool move_editor) {
	if (!context->originalSubs || !context->originalSubs->IsOpen())
		return;

	int i = 0;
	AssDialogue *target = nullptr;
	for (auto& line : context->ass->Events) {
		if (i++ == row) {
			target = &line;
			break;
		}
	}
	if (!target)
		return;

	context->originalSubs->SetActiveRow(row);
	context->selectionController->SetSelectionAndActive(Selection{target}, target);

	if (move_editor && context->subsEditBox)
		context->subsEditBox->SetEditingOriginal(true);
}

void OriginalGrid::OnMouseWheel(wxMouseEvent &event) {
	if (!context->subsGrid || !context->originalSubs || !context->originalSubs->IsOpen() ||
		event.GetWheelRotation() == 0) {
		event.Skip();
		return;
	}

	int rotation = event.GetWheelRotation();

	// Scrolling the pane scrolls the subtitle grid, which in turn moves the
	// pane, so that both stay lined up
	int lines = std::max(1, event.GetLinesPerAction());
	context->subsGrid->ScrollTo(yPos - (rotation > 0 ? lines : -lines));
}

void OriginalGrid::OnMouse(wxMouseEvent &event) {
	if (!context->originalSubs || !context->originalSubs->IsOpen()) {
		event.Skip();
		return;
	}

	int row = RowAtPoint(event.GetY());
	if (row < 0 || (size_t)row >= context->originalSubs->Lines().size()) {
		event.Skip();
		return;
	}

	if (event.LeftDown() || event.LeftDClick()) {
		SetFocus();
		SelectPairedLine(row, event.LeftDClick());
	}
}

void OriginalGrid::OnPaint(wxPaintEvent &) {
	int w = 0, h = 0;
	GetClientSize(&w, &h);

	wxAutoBufferedPaintDC dc(this);
	dc.SetFont(font);

	auto background = to_wx(OPT_GET("Colour/Subtitle Grid/Background/Background")->GetColor());
	auto header = to_wx(OPT_GET("Colour/Subtitle Grid/Header")->GetColor());
	auto selection = to_wx(OPT_GET("Colour/Subtitle Grid/Background/Selection")->GetColor());
	auto text_standard = to_wx(OPT_GET("Colour/Subtitle Grid/Standard")->GetColor());
	auto text_selection = to_wx(OPT_GET("Colour/Subtitle Grid/Selection")->GetColor());
	auto grid_pen = wxPen(to_wx(OPT_GET("Colour/Subtitle Grid/Lines")->GetColor()));

	dc.SetBackground(wxBrush(background));
	dc.Clear();
	dc.SetPen(*wxTRANSPARENT_PEN);

	// The empty header keeps the pane's rows lined up with the grid's rows
	dc.SetBrush(wxBrush(header));
	dc.DrawRectangle(0, 0, w, lineHeight);

	dc.SetPen(grid_pen);
	dc.DrawLine(0, lineHeight, w, lineHeight);

	auto const& lines = context->originalSubs->Lines();
	int rows = (int)lines.size();
	auto *active = context->selectionController->GetActiveLine();
	int active_row = active ? active->Row : -1;

	// Same rows as the subtitle grid draws, so that the two panes stay in step
	int n_draw = agi::util::mid(0, h / lineHeight + 1, rows - yPos);
	for (int i = 0; i < n_draw; ++i) {
		int row = i + yPos;
		if (row < 0 || row >= rows)
			continue;

		int y = (i + 1) * lineHeight;
		bool is_active = row == active_row;

		dc.SetPen(*wxTRANSPARENT_PEN);
		if (is_active) {
			dc.SetBrush(wxBrush(selection));
			dc.DrawRectangle(0, y, w, lineHeight);
			dc.SetTextForeground(text_selection);
		}
		else {
			dc.SetTextForeground(text_standard);
		}

		// The text is drawn left aligned and untransformed, exactly like the
		// subtitle grid draws it, so that the two panes can be compared line by
		// line
		auto text = to_wx(lines[row]->Text.get());
		if (!text.empty())
			dc.DrawText(text, 4, y + 2);

		dc.SetPen(grid_pen);
		dc.DrawLine(0, y + lineHeight, w, y + lineHeight);
	}

	if (rows == 0) {
		dc.SetTextForeground(text_standard);
		dc.DrawText(_("No original subtitle is open."), 6, lineHeight + 4);
	}
}
