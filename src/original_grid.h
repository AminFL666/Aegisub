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

/// @file original_grid.h
/// @brief The original subtitle pane shown on the right of the main window
/// @ingroup main_ui

#pragma once

#include <libaegisub/signal.h>

#include <vector>

#include <wx/event.h>
#include <wx/font.h>
#include <wx/window.h>

class wxScrollBar;
class wxDC;
namespace agi { struct Context; }

/// @class OriginalGrid
/// @brief Read-only timing with editable text of the paired original subtitle
///
/// Shows the numbered lines of the original subtitle next to the current file.
/// Clicking a row selects the paired line of the current file; double-clicking
/// moves the text edit box to the original.
class OriginalGrid final : public wxWindow, private agi::signal::ConnectionScope {
	agi::Context *context;

	wxScrollBar *scrollBar;
	wxFont font;
	int lineHeight = 16;
	int yPos = 0;

	int width_number = 40;
	int width_time = 80;

	std::vector<agi::signal::Connection> connections;

	void OnPaint(wxPaintEvent &event);
	void OnSize(wxSizeEvent &event);
	void OnScroll(wxScrollEvent &event);
	void OnMouse(wxMouseEvent &event);
	void OnKeyDown(wxKeyEvent &event);
	void OnMouseWheel(wxMouseEvent &event);
	void OnDPIChanged(wxDPIChangedEvent &event);
	void OnActiveLineChanged();

	void AdjustScrollbar();
	void ScrollTo(int position);
	void UpdateStyle();
	void SetColumnWidths(wxDC &dc);

	int RowAtPoint(int y) const;
	/// Select the current-file line paired with a row of the original
	void SelectPairedLine(int row, bool move_editor);
	void PaintCell(wxDC &dc, wxString const& text, int x, int y, int width, bool right_aligned);

public:
	OriginalGrid(wxWindow *parent, agi::Context *context);
	~OriginalGrid();

	/// Scroll so that the given row is visible
	void MakeRowVisible(int row);
};
