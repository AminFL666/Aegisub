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

#include <wx/font.h>
#include <wx/window.h>

class wxDC;
namespace agi { struct Context; }

/// @class OriginalGrid
/// @brief The text of the original subtitle, shown next to the current file
///
/// The pane shows only the text, as the numbering and the timings are always
/// the same as the current file's. It has no scrollbar of its own: it is
/// scrolled together with the subtitle grid, so that the two always line up.
class OriginalGrid final : public wxWindow, private agi::signal::ConnectionScope {
	agi::Context *context;

	wxFont font;
	int lineHeight = 16;
	int yPos = 0;

	std::vector<agi::signal::Connection> connections;

	void OnPaint(wxPaintEvent &event);
	void OnSize(wxSizeEvent &event);
	void OnMouse(wxMouseEvent &event);
	void OnMouseWheel(wxMouseEvent &event);
	void OnKeyDown(wxKeyEvent &event);
	void OnDPIChanged(wxDPIChangedEvent &event);
	void OnShow(wxShowEvent &event);
	void OnActiveLineChanged();

	void UpdateStyle();

	int RowAtPoint(int y) const;
	/// Select the current-file line paired with a row of the original
	void SelectPairedLine(int row, bool move_editor);

public:
	OriginalGrid(wxWindow *parent, agi::Context *context);
	~OriginalGrid();

	/// @brief Scroll the pane to the row the subtitle grid starts at
	///
	/// Called by the subtitle grid whenever it scrolls, as the pane has no
	/// scrollbar and always follows it.
	void SyncScroll(int y);

	/// Scroll so that the given row is visible, scrolling the subtitle grid
	/// rather than the pane
	void MakeRowVisible(int row);
};
