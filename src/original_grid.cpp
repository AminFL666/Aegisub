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

#include "original_grid.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "compat.h"
#include "include/aegisub/context.h"
#include "options.h"
#include "rtl.h"
#include "selection_controller.h"
#include "subs_edit_box.h"
#include "subs_original.h"
#include "utils.h"

#include <libaegisub/util.h>

#include <algorithm>

#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/intl.h>
#include <wx/scrolbar.h>
#include <wx/settings.h>
#include <wx/sizer.h>

namespace {
enum {
	ORIGINAL_GRID_SCROLLBAR = 1740
};
}

OriginalGrid::OriginalGrid(wxWindow *parent, agi::Context *context)
: wxWindow(parent, -1, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS | wxSUNKEN_BORDER)
, context(context)
, scrollBar(new wxScrollBar(this, ORIGINAL_GRID_SCROLLBAR, wxDefaultPosition, wxDefaultSize, wxSB_VERTICAL))
{
	scrollBar->SetScrollbar(0, 10, 100, 10);

	auto sizer = new wxBoxSizer(wxHORIZONTAL);
	sizer->AddStretchSpacer();
	sizer->Add(scrollBar, 0, wxEXPAND, 0);
	SetSizer(sizer);

	SetBackgroundStyle(wxBG_STYLE_PAINT);

	UpdateStyle();

	connections = agi::signal::make_vector({
		context->selectionController->AddActiveLineListener([this] { OnActiveLineChanged(); }),
		context->selectionController->AddSelectionListener([this] { Refresh(false); }),
		context->originalSubs->AddChangeListener([this] { AdjustScrollbar(); Refresh(false); }),
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

	// There is no catch-all scroll event, so every event which can move the
	// thumb has to be handled
	scrollBar->Bind(wxEVT_SCROLL_TOP, &OriginalGrid::OnScroll, this);
	scrollBar->Bind(wxEVT_SCROLL_BOTTOM, &OriginalGrid::OnScroll, this);
	scrollBar->Bind(wxEVT_SCROLL_LINEUP, &OriginalGrid::OnScroll, this);
	scrollBar->Bind(wxEVT_SCROLL_LINEDOWN, &OriginalGrid::OnScroll, this);
	scrollBar->Bind(wxEVT_SCROLL_PAGEUP, &OriginalGrid::OnScroll, this);
	scrollBar->Bind(wxEVT_SCROLL_PAGEDOWN, &OriginalGrid::OnScroll, this);
	scrollBar->Bind(wxEVT_SCROLL_THUMBTRACK, &OriginalGrid::OnScroll, this);
	scrollBar->Bind(wxEVT_SCROLL_THUMBRELEASE, &OriginalGrid::OnScroll, this);
}

OriginalGrid::~OriginalGrid() = default;

void OriginalGrid::UpdateStyle() {
	font = *wxNORMAL_FONT;
	font.SetEncoding(wxFONTENCODING_DEFAULT);
	wxString fontname = FontFace("Subtitle/Grid");
	if (!fontname.empty())
		font.SetFaceName(fontname);
	font.SetPointSize(OPT_GET("Subtitle/Grid/Font Size")->GetInt());

	wxClientDC dc(this);
	dc.SetFont(font);
	lineHeight = dc.GetCharHeight() + 4;

	SetColumnWidths(dc);
	AdjustScrollbar();
	Refresh(false);
}

void OriginalGrid::SetColumnWidths(wxDC &dc) {
	dc.SetFont(font);
	width_number = std::max(FromDIP(32), dc.GetTextExtent(wxS("0000")).GetWidth() + 8);
	width_time = std::max(FromDIP(64), dc.GetTextExtent(wxS("0:00:00.00")).GetWidth() + 10);
}

void OriginalGrid::AdjustScrollbar() {
	int rows = context->originalSubs ? context->originalSubs->Lines().size() : 0;
	int page = std::max(1, GetClientSize().GetHeight() / lineHeight);
	yPos = agi::util::mid(0, yPos, std::max(0, rows - page));
	scrollBar->SetScrollbar(yPos, page, rows + 1, page);
}

void OriginalGrid::ScrollTo(int position) {
	if (position == yPos)
		return;

	yPos = position;
	AdjustScrollbar();
	Refresh(false);
}

void OriginalGrid::MakeRowVisible(int row) {
	int h = GetClientSize().GetHeight();

	if (row < yPos + 1)
		ScrollTo(row - 1);
	else if (row > yPos + h / lineHeight - 3)
		ScrollTo(row - h / lineHeight + 3);
}

int OriginalGrid::RowAtPoint(int y) const {
	return y / lineHeight + yPos - 1;
}

void OriginalGrid::OnScroll(wxScrollEvent &event) {
	ScrollTo(event.GetPosition());
}

void OriginalGrid::OnSize(wxSizeEvent &) {
	AdjustScrollbar();
	Refresh(false);
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
	if (!context->originalSubs || !context->originalSubs->IsOpen())
		return;

	int rotation = event.GetWheelRotation();
	if (rotation == 0)
		return;

	// Scroll the direction the wheel was turned in, a few lines at a time
	int lines = std::max(1, event.GetLinesPerAction());
	ScrollTo(yPos - (rotation > 0 ? lines : -lines));
}

void OriginalGrid::OnMouse(wxMouseEvent &event) {
	if (!context->originalSubs || !context->originalSubs->IsOpen())
		return;

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

void OriginalGrid::PaintCell(wxDC &dc, wxString const& text, int x, int y, int width, bool right_aligned) {
	if (text.empty())
		return;

	int available = width - 8;
	if (right_aligned) {
		wxString clipped = text;
		while (!clipped.empty() && dc.GetTextExtent(clipped).GetWidth() > available)
			clipped.RemoveLast();
		dc.DrawText(clipped, x + width - 4 - dc.GetTextExtent(clipped).GetWidth(), y + 2);
	}
	else {
		dc.SetClippingRegion(x, y, width, lineHeight);
		dc.DrawText(text, x + 4, y + 2);
		dc.DestroyClippingRegion();
	}
}

void OriginalGrid::OnPaint(wxPaintEvent &) {
	int w = 0, h = 0;
	GetClientSize(&w, &h);
	w -= scrollBar->GetSize().GetWidth();

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

	// Header
	dc.SetBrush(wxBrush(header));
	dc.DrawRectangle(0, 0, w, lineHeight);
	dc.SetTextForeground(text_standard);
	PaintCell(dc, "#", 0, 0, width_number, true);
	PaintCell(dc, _("Start"), width_number, 0, width_time, true);
	PaintCell(dc, _("End"), width_number + width_time, 0, width_time, true);
	PaintCell(dc, _("Text"), width_number + width_time * 2, 0, std::max(0, w - width_number - width_time * 2), false);

	dc.SetPen(grid_pen);
	dc.DrawLine(0, lineHeight, w, lineHeight);

	auto const& lines = context->originalSubs->Lines();
	int rows = (int)lines.size();
	auto *active = context->selectionController->GetActiveLine();
	int active_row = active ? active->Row : -1;

	int n_draw = agi::util::mid(0, h / lineHeight, rows - yPos);
	for (int i = 0; i < n_draw; ++i) {
		int row = i + yPos;
		int y = (i + 1) * lineHeight;
		bool is_active = row == active_row;

		if (is_active) {
			dc.SetBrush(wxBrush(selection));
			dc.DrawRectangle(0, y, w, lineHeight);
			dc.SetTextForeground(text_selection);
		}
		else {
			dc.SetTextForeground(text_standard);
		}
		dc.SetPen(*wxTRANSPARENT_PEN);

		wxString number = wxString::Format("%d", row + 1);
		PaintCell(dc, number, 0, y, width_number, true);

		if (row < (int)lines.size()) {
			AssDialogue *line = lines[row];
			PaintCell(dc, to_wx(line->Start.GetAssFormatted()), width_number, y, width_time, true);
			PaintCell(dc, to_wx(line->End.GetAssFormatted()), width_number + width_time, y, width_time, true);

			auto text = to_wx(rtl::ToPlainText(line->Text.get(), false));
			bool rtl = rtl::IsRTL(line->Text.get());
			PaintCell(dc, text, width_number + width_time * 2, y,
				std::max(0, w - width_number - width_time * 2), rtl);
		}

		dc.SetPen(grid_pen);
		dc.DrawLine(0, y + lineHeight, w, y + lineHeight);
	}

	// Vertical separators
	dc.SetPen(grid_pen);
	dc.DrawLine(width_number, 0, width_number, (n_draw + 1) * lineHeight);
	dc.DrawLine(width_number + width_time, 0, width_number + width_time, (n_draw + 1) * lineHeight);
	dc.DrawLine(width_number + width_time * 2, 0, width_number + width_time * 2, (n_draw + 1) * lineHeight);

	if (rows == 0) {
		dc.SetTextForeground(text_standard);
		dc.DrawText(_("No original subtitle is open."), 6, lineHeight + 4);
	}
}
