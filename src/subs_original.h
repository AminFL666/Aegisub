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

/// @file subs_original.h
/// @brief Companion "original subtitle" file paired with the current file
/// @ingroup subs_storage

#pragma once

#include "rtl.h"

#include <libaegisub/fs.h>
#include <libaegisub/signal.h>

#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class AssDialogue;
class AssFile;
namespace agi { struct Context; }

/// @class OriginalSubsController
/// @brief The reference subtitle file shown in the original subtitle pane
///
/// The original file is paired with the current file strictly by row number:
/// the i-th line of the original corresponds to the i-th line of the current
/// file. Structure (line count, order) and timing flow one way, from the
/// current file into the original; text is edited independently in both.
///
/// Pairing is tracked by the unique id of the current file's lines rather than
/// by index alone, so sorting, undo and redo of the current file keep each
/// original line with the line it was written for.
class OriginalSubsController : private agi::signal::ConnectionScope {
public:
	explicit OriginalSubsController(agi::Context *context);
	~OriginalSubsController();

	/// Is an original subtitle file open?
	bool IsOpen() const { return file != nullptr; }

	/// The original file, or nullptr if none is open
	AssFile *File() const { return file.get(); }

	/// Lines of the original file in file order: the paired lines first, then
	/// any lines left over because the original is longer than the current file
	std::vector<AssDialogue*> const& Lines() const { return all_lines; }

	/// Row of the current file a row of the original is paired with, or -1
	int CurrentRowOf(int original_row) const;

	/// Original line for a row of the current file, or nullptr
	AssDialogue *LineForCurrentRow(int row) const;

	/// The path of the open original file
	agi::fs::path Filename() const { return filename; }

	/// The extension (including the dot) the file was opened with
	std::string const& FormatExtension() const { return format_extension; }

	/// Does the original have unsaved changes?
	bool IsModified() const { return modified; }

	/// The direction of the original text, detected for the file as a whole
	rtl::Direction Direction() const { return direction; }

	/// @brief Open a file, replacing any currently open original
	void Open(agi::fs::path const& path);

	/// @brief Close the original without asking, discarding changes
	void Close();

	/// @brief Ask to save the original if it has unsaved changes
	/// @return false if the user cancelled, so the caller should abort
	bool TryToClose();

	/// Save to the current filename, prompting for a name if there is none
	void Save();
	/// Save to a new filename chosen by the user
	void SaveAs();
	/// Save to a specific file
	/// @throw agi::Exception on failure
	void SaveTo(agi::fs::path const& path);

	/// Replace the text of a line of the original
	/// @param row Row of the original
	/// @param text New raw ASS text
	/// @param amend Coalesce with the previous edit of the same line
	void SetLineText(int row, std::string const& text, bool amend = false);

	void Undo();
	void Redo();
	bool CanUndo() const { return !undo_stack.empty(); }
	bool CanRedo() const { return !redo_stack.empty(); }

	/// Is the given original row edited by the text edit box?
	int ActiveRow() const { return active_row; }
	void SetActiveRow(int row) { active_row = row; }

	/// @brief Build the file handed to the video preview
	///
	/// This is a copy of the current file plus the lines of the original, so
	/// that the original is rendered at the top of the video with its own
	/// "Original" style.
	std::unique_ptr<AssFile> MakePreviewFile() const;

	/// Write an autosave copy if the original has unsaved changes
	void AutoSave() const;

	/// A line of the original changed
	agi::signal::Signal<> AnnounceChange;
	/// The original was opened or closed
	agi::signal::Signal<> AnnounceOpen;

	DEFINE_SIGNAL_ADDERS(AnnounceChange, AddChangeListener)
	DEFINE_SIGNAL_ADDERS(AnnounceOpen, AddOpenListener)

private:
	agi::Context *context;

	std::unique_ptr<AssFile> file;
	agi::fs::path filename;
	std::string format_extension;
	bool modified = false;
	rtl::Direction direction = rtl::Direction::LTR;

	/// Lines paired with the current file, in order
	std::vector<AssDialogue*> lines;
	/// Current file line id of each entry in lines (parallel array)
	std::vector<int> line_ids;
	/// lines followed by unpaired, i.e. everything the pane shows
	std::vector<AssDialogue*> all_lines;
	/// Original lines which have no partner because the original has more
	/// lines than the current file
	std::vector<AssDialogue*> unpaired;

	/// Current file line id -> original line
	std::unordered_map<int, AssDialogue*> by_id;
	/// Text of original lines whose current file line was deleted, so that
	/// undoing the deletion can bring it back
	std::unordered_map<int, std::string> removed;

	struct TextEdit {
		int id;
		std::string before;
		std::string after;
	};
	std::deque<TextEdit> undo_stack;
	std::deque<TextEdit> redo_stack;

	/// Reentrancy guard for the commit listener
	bool syncing = false;
	/// Keep original lines which lose their partner instead of deleting them,
	/// used when the current file as a whole is being replaced
	bool keep_orphans = false;
	int active_row = 0;

	agi::signal::Connection commit_connection;
	agi::signal::Connection file_open_connection;

	/// @brief Make the original follow the current file
	/// @param initial Pair every line instead of only the ones which changed
	void SyncToCurrentFile(bool initial);

	/// @brief Follow the current file where it has split a line in several
	/// @param old_lines Pairing before this sync, by row
	/// @param old_ids Line ids of old_lines
	/// @param current_rows Row of each line id of the current file
	/// @param claimed Original lines which already have a line of their own
	/// @param pieces Current line id -> original line to use for it
	///
	/// A tool or a script which splits a line replaces it with several new
	/// lines, so the original line loses the line it was paired with. Its
	/// text is split over the new lines, which keeps the translation lined
	/// up with the file it belongs to.
	void SplitFollowCurrent(std::vector<AssDialogue*> const& old_lines,
		std::vector<int> const& old_ids,
		std::unordered_map<int, int> const& current_rows,
		std::unordered_set<AssDialogue*> &claimed,
		std::unordered_map<int, AssDialogue*> &pieces);

	/// Commit listener for the current file
	void OnCurrentCommit(int type);
	/// The current file was replaced (opened or closed)
	void OnCurrentFileOpened();
	/// Pair every original line with the current file again, by row
	void RepairByRow();

	/// @brief Create or refresh the "Original" style in the current file
	void EnsureStyle();
	/// @brief Add the "Original" style to a copy of a file used for preview or saving
	void EnsureStyleIn(AssFile &target) const;

	AssDialogue *LineById(int id) const;
	void SetModified(bool state);

	void WriteSrt(agi::fs::path const& path) const;
	void UpdateDirection();
};
