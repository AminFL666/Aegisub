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

#include "subs_original.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_style.h"
#include "charset_detect.h"
#include "compat.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "options.h"
#include "project.h"
#include "subs_controller.h"
#include "subtitle_format.h"
#include "text_file_writer.h"
#include "utils.h"
#include "video_controller.h"

#include <libaegisub/exception.h>
#include <libaegisub/format.h>
#include <libaegisub/log.h>
#include <libaegisub/util.h>

#include <boost/algorithm/string/case_conv.hpp>

#include <algorithm>
#include <limits>
#include <unordered_set>

#include <wx/intl.h>
#include <wx/msgdlg.h>

namespace {
/// The placeholder text of a line which has no counterpart in the other file.
/// A zero-width space, so that the line looks empty but can still be selected
/// and typed into.
const char *const placeholder_text = "\xE2\x80\x8B";

/// How many deleted original lines to remember so that undoing a delete can
/// bring their text back
constexpr size_t max_remembered_deleted = 200;

/// @brief Keep a line which is empty from being dropped when saving
///
/// Subtitle formats skip lines with no text at all, so an empty line of the
/// original is stored with a zero-width space in it instead. The line then
/// still exists in the saved file, with the same timing as the current line.
std::string EnsureNonEmpty(std::string text) {
	if (text.empty())
		return placeholder_text;
	return text;
}

/// @brief Move a position in a UTF-8 string to the start of a character
size_t SnapToCharacter(std::string const& text, size_t pos) {
	while (pos > 0 && pos < text.size() &&
		(static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80)
		++pos;
	return pos;
}

/// @brief Move a position out of an override block
///
/// Cutting through a {} block would leave half of its tags on either side of
/// the cut, so the position is moved to just after the block instead.
size_t LeaveOverrideBlock(std::string const& text, size_t pos) {
	auto open = text.rfind('{', pos);
	if (open == std::string::npos)
		return pos;

	auto close = text.find('}', open);
	if (close != std::string::npos && pos <= close)
		return close + 1;

	return pos;
}

/// @brief Split the text of an original line into several lines
/// @param text Raw ASS text
/// @param count Number of pieces to split it into
/// @return The pieces, which are never more than @a count long
///
/// The line breaks of the text are the natural place to cut, as the
/// translation already ends a line there. When there are not enough of them
/// the text is cut at a word boundary near an equal share of its length.
std::vector<std::string> SplitText(std::string const& text, size_t count) {
	std::vector<std::string> pieces;
	if (count <= 1) {
		pieces.push_back(text);
		return pieces;
	}

	// Positions just after a line break
	std::vector<size_t> breaks;
	for (size_t i = 0; i + 1 < text.size(); ++i) {
		if (text[i] == '\\' && (text[i + 1] == 'N' || text[i + 1] == 'n'))
			breaks.push_back(i + 2);
	}

	// Cut at line breaks when there are enough of them
	std::vector<size_t> cuts;
	if (breaks.size() >= count - 1) {
		for (size_t i = 1; i < count; ++i) {
			size_t target = text.size() * i / count;
			size_t from = cuts.empty() ? 0 : cuts.back();
			size_t best = 0, best_dist = std::numeric_limits<size_t>::max();
			for (auto pos : breaks) {
				if (pos <= from)
					continue;

				size_t dist = pos > target ? pos - target : target - pos;
				if (dist < best_dist) {
					best_dist = dist;
					best = pos;
				}
			}
			if (best == 0)
				break;
			cuts.push_back(best);
		}
	}

	// Otherwise cut at a word boundary near an equal share of the text
	if (cuts.size() < count - 1) {
		cuts.clear();
		for (size_t i = 1; i < count; ++i) {
			size_t from = cuts.empty() ? 0 : cuts.back();
			size_t target = LeaveOverrideBlock(text, SnapToCharacter(text, text.size() * i / count));

			// Look for a space near the target position
			size_t window = text.size() / 8 + 1;
			size_t best = 0;
			for (size_t d = 0; d <= window; ++d) {
				if (target > d && target - d > from && text[target - d - 1] == ' ') {
					best = target - d;
					break;
				}
				if (target + d < text.size() && text[target + d] == ' ') {
					best = target + d + 1;
					break;
				}
			}

			// The space which was found may be inside an override block as
			// well, so leave the block before cutting there
			if (best)
				best = LeaveOverrideBlock(text, best);

			if (best <= from || best >= text.size())
				best = LeaveOverrideBlock(text, SnapToCharacter(text, text.size() * i / count));
			if (best <= from || best > text.size())
				best = text.size() * i / count;

			cuts.push_back(best);
		}
	}

	size_t start = 0;
	for (auto cut : cuts) {
		pieces.push_back(text.substr(start, cut - start));
		start = cut;
	}
	pieces.push_back(text.substr(start));

	for (auto& piece : pieces) {
		// Tidy up the space the cut left behind, without touching the tags
		auto first = piece.find_first_not_of(" \t");
		if (first == std::string::npos)
			piece.clear();
		else
			piece = piece.substr(first, piece.find_last_not_of(" \t") - first + 1);
	}

	return pieces;
}

} // namespace

OriginalSubsController::OriginalSubsController(agi::Context *context)
: context(context)
, commit_connection(context->ass->AddCommitListener(&OriginalSubsController::OnCurrentCommit, this))
, file_open_connection(context->subsController->AddFileOpenListener(&OriginalSubsController::OnCurrentFileOpened, this))
{
}

OriginalSubsController::~OriginalSubsController() = default;

void OriginalSubsController::OnCurrentCommit(int type) {
	if (!file)
		return;

	// Editing the text of the current file does not affect the original, and
	// the pairing does not change, so there is nothing to do
	if (type == AssFile::COMMIT_DIAG_TEXT)
		return;

	// A whole-file commit comes either from opening a different file or from
	// an undo. Original lines must survive it either way, so that undo can
	// restore them and so that a new current file does not discard the text
	keep_orphans = type == AssFile::COMMIT_NEW;
	SyncToCurrentFile(false);
	keep_orphans = false;
}

void OriginalSubsController::OnCurrentFileOpened() {
	RepairByRow();
}

void OriginalSubsController::RepairByRow() {
	if (!file)
		return;

	// Take every line back out of the pairing, in file order, and pair them
	// with the new current file from the top
	std::vector<AssDialogue*> all;
	all.reserve(file->Events.size());
	for (auto& line : file->Events)
		all.push_back(&line);

	lines.clear();
	line_ids.clear();
	all_lines.clear();
	by_id.clear();
	removed.clear();
	unpaired = std::move(all);

	SyncToCurrentFile(true);

	// The preview has to be rebuilt for the new current file, as the lines
	// which were paired with the old one are gone
	context->videoController->ReloadSubtitles();
}

AssDialogue *OriginalSubsController::LineById(int id) const {
	for (size_t i = 0; i < line_ids.size(); ++i) {
		if (line_ids[i] == id)
			return lines[i];
	}
	return nullptr;
}

int OriginalSubsController::CurrentRowOf(int original_row) const {
	if (original_row < 0 || (size_t)original_row >= line_ids.size())
		return -1;
	return original_row;
}

AssDialogue *OriginalSubsController::LineForCurrentRow(int row) const {
	if (row < 0 || (size_t)row >= line_ids.size())
		return nullptr;

	// The pairing is only valid while the current file's rows are unchanged
	// since the last sync, so verify the id when possible
	int i = 0;
	for (auto const& line : context->ass->Events) {
		if (i++ == row)
			return line.Id == line_ids[row] ? lines[row] : nullptr;
	}

	return nullptr;
}

void OriginalSubsController::SetModified(bool state) {
	modified = state;
}

void OriginalSubsController::UpdateDirection() {
	direction = file ? rtl::DetectMajority(*file) : rtl::Direction::LTR;
}

void OriginalSubsController::Open(agi::fs::path const& path) {
	// Detect the character set exactly like opening the file as the current
	// subtitles does; passing no encoding at all would read the file as if it
	// were in the system character set, which breaks UTF-16 files and mangles
	// anything that is not ASCII.
	std::string encoding;
	try {
		encoding = CharSetDetect::GetEncoding(path);
	}
	catch (agi::UserCancelException const&) {
		// The user closed the character set picker, so nothing was opened
		return;
	}

	auto reader = SubtitleFormat::GetReader(path, encoding.c_str());
	if (!reader)
		throw agi::InvalidInputException("Unknown file type.");

	auto new_file = std::make_unique<AssFile>();
	try {
		reader->ReadFile(new_file.get(), path, context->project->Timecodes(), encoding.c_str());
	}
	catch (agi::Exception const&) {
		// Character set detection can guess wrong for a plain UTF-8 file with
		// no byte order mark, so give that a try before giving up
		if (encoding == "utf-8")
			throw;

		new_file = std::make_unique<AssFile>();
		reader->ReadFile(new_file.get(), path, context->project->Timecodes(), "utf-8");
	}

	Close();

	file = std::move(new_file);
	filename = path;
	format_extension = boost::to_lower_copy(path.extension().string());
	if (format_extension.empty())
		format_extension = ".ass";

	undo_stack.clear();
	redo_stack.clear();
	removed.clear();
	by_id.clear();
	lines.clear();
	line_ids.clear();
	all_lines.clear();
	unpaired.clear();
	for (auto& line : file->Events) {
		line.Style = "Original";
		unpaired.push_back(&line);
	}

	SetModified(false);

	// The original style has to exist before anything is paired so that the
	// video preview can render the original at the top of the screen
	EnsureStyle();
	SyncToCurrentFile(true);

	context->videoController->ReloadSubtitles();

	OPT_SET("Path/Last/Original Subtitles")->SetString(path.parent_path().string());

	AnnounceOpen();
	AnnounceChange();

	int original_count = (int)(lines.size() + unpaired.size());
	int current_count = (int)context->ass->Events.size();
	if (original_count != current_count) {
		wxMessageBox(
			fmt_tl("The original subtitle has %d lines but the current file has %d.\n\n"
			       "Lines are paired by row number, so only the first %d lines are paired. "
			       "Empty placeholder lines fill in for the lines the current file is missing.",
			       original_count, current_count, std::min(original_count, current_count)),
			_("Line counts do not match"), wxOK | wxICON_INFORMATION, context->parent);
	}
}

void OriginalSubsController::Close() {
	bool was_open = file != nullptr;

	file.reset();
	lines.clear();
	line_ids.clear();
	all_lines.clear();
	unpaired.clear();
	by_id.clear();
	removed.clear();
	undo_stack.clear();
	redo_stack.clear();
	filename.clear();
	format_extension.clear();
	direction = rtl::Direction::LTR;
	active_row = 0;
	SetModified(false);

	if (was_open) {
		context->videoController->ReloadSubtitles();
		AnnounceOpen();
		AnnounceChange();
	}
}

bool OriginalSubsController::TryToClose() {
	if (!file || !modified)
		return true;

	wxString name = filename.empty() ? _("Untitled") : to_wx(filename.filename().string());
	int result = wxMessageBox(
		fmt_tl("Do you want to save changes to %s?", name),
		_("Unsaved changes"), wxYES_NO | wxCANCEL | wxICON_QUESTION, context->parent);

	if (result == wxCANCEL)
		return false;

	if (result == wxYES) {
		try {
			Save();
		}
		catch (agi::Exception const& err) {
			wxMessageBox(to_wx(err.GetMessage()), _("Error"), wxOK | wxICON_ERROR | wxCENTER, context->parent);
			return false;
		}
		catch (...) {
			wxMessageBox(_("Unknown error"), _("Error"), wxOK | wxICON_ERROR | wxCENTER, context->parent);
			return false;
		}
		return !modified;
	}

	return true;
}

void OriginalSubsController::Save() {
	if (filename.empty()) {
		SaveAs();
		return;
	}

	SaveTo(filename);
}

void OriginalSubsController::SaveAs() {
	auto ext = format_extension.empty() ? std::string(".ass") : format_extension;
	auto stem = filename.empty() ? std::string("original") : filename.stem().string();

	agi::fs::path probe = filename.empty() ? agi::fs::path("untitled" + ext) : filename;
	auto writer = SubtitleFormat::GetWriter(probe);
	std::string wildcard = agi::format(
		"%s (*%s)|*%s|", writer ? std::string(writer->GetName()) : std::string("SubRip"), ext, ext);
	wildcard += SubtitleFormat::GetWildcards(1);

	auto path = SaveFileSelector(context, _("Save Original Subtitle File"),
		"Path/Last/Original Subtitles", stem + ext, ext.substr(1), wildcard, context->parent);
	if (path.empty())
		return;

	SaveTo(path);
}

void OriginalSubsController::SaveTo(agi::fs::path const& path) {
	if (!file)
		return;

	auto ext = boost::to_lower_copy(path.extension().string());
	if (ext == ".srt") {
		WriteSrt(path);
	}
	else {
		auto writer = SubtitleFormat::GetWriter(path);
		if (!writer)
			throw agi::InvalidInputException("Unknown file type.");

		AssFile copy(*file);
		EnsureStyleIn(copy);
		for (auto& line : copy.Events)
			line.Text = EnsureNonEmpty(line.Text.get());
		writer->WriteFile(&copy, path, context->project->Timecodes());
	}

	filename = path;
	format_extension = ext.empty() ? ".ass" : ext;
	SetModified(false);
	OPT_SET("Path/Last/Original Subtitles")->SetString(path.parent_path().string());

	AnnounceOpen();
}

void OriginalSubsController::WriteSrt(agi::fs::path const& path) const {
	TextFileWriter out(path);

	int i = 0;
	for (auto const& line : file->Events) {
		out.WriteLineToFile(std::to_string(++i));
		out.WriteLineToFile(line.Start.GetSrtFormatted() + " --> " + line.End.GetSrtFormatted());
		out.WriteLineToFile(EnsureNonEmpty(rtl::ToPlainText(line.Text.get())));
		out.WriteLineToFile("");
	}
}

void OriginalSubsController::SyncToCurrentFile(bool initial) {
	if (!file || syncing)
		return;

	syncing = true;

	// The pairing as it was before this sync, by row
	std::vector<AssDialogue*> const old_lines = lines;
	std::vector<int> const old_ids = line_ids;

	// Originals which are already spoken for, either because their own line
	// is still there or because they have been given to another line below
	std::unordered_set<AssDialogue*> claimed;

	auto const& current = context->ass->Events;

	// Row of each line of the current file
	std::unordered_map<int, int> current_rows;
	{
		int row = 0;
		for (auto const& cur : current)
			current_rows.emplace(cur.Id, row++);
	}

	// Originals whose line is still in the current file keep it
	for (size_t i = 0; i < old_lines.size() && i < old_ids.size(); ++i) {
		if (current_rows.count(old_ids[i]))
			claimed.insert(old_lines[i]);
	}

	// Lines which a tool or a script has split into several. Splitting a line
	// uses several lines where there was one, so it can only have happened
	// when the number of lines is not the same as before; checking this keeps
	// the common case of a timing change from searching through the file.
	std::unordered_map<int, AssDialogue*> pieces;
	if (!initial && current.size() != old_lines.size())
		SplitFollowCurrent(old_lines, old_ids, current_rows, claimed, pieces);

	std::vector<AssDialogue*> new_lines;
	std::vector<int> new_ids;
	std::unordered_map<int, AssDialogue*> new_by_id;
	std::vector<AssDialogue*> to_delete;

	new_lines.reserve(current.size());
	new_ids.reserve(current.size());

	int row = 0;
	for (auto const& cur : current) {
		AssDialogue *orig = nullptr;

		auto piece = pieces.find(cur.Id);
		if (piece != pieces.end()) {
			orig = piece->second;
		}
		else {
			auto paired = by_id.find(cur.Id);
			if (paired != by_id.end()) {
				orig = paired->second;
				claimed.insert(orig);
			}
			else {
				auto resurrected = removed.find(cur.Id);
				if (resurrected != removed.end()) {
					// The line was deleted and is back, most likely because the
					// deletion was undone, so restore its text
					orig = new AssDialogue;
					orig->Text = EnsureNonEmpty(resurrected->second);
					removed.erase(resurrected);
				}
				else if (row < (int)old_lines.size() && !claimed.count(old_lines[row])) {
					// The line which used to sit here is gone and was replaced
					// by one with a new id, which is what a script or a tool
					// rewriting lines does. Keeping the text paired with the
					// row stops the original from being wiped by a script
					// which only meant to change timing.
					orig = old_lines[row];
				}
				else if (!unpaired.empty()) {
					// The original has more lines than the current file had
					// before
					orig = unpaired.front();
					unpaired.erase(unpaired.begin());
				}
				else {
					orig = new AssDialogue;
					orig->Text = placeholder_text;
				}
				claimed.insert(orig);
			}
		}

		// Timing and the metadata which has to match the current line. Text,
		// style, comment and layer are deliberately left alone.
		orig->Start = cur.Start;
		orig->End = cur.End;
		orig->Actor = cur.Actor;
		orig->Effect = cur.Effect;
		orig->Margin = cur.Margin;
		orig->Style = "Original";

		new_lines.push_back(orig);
		new_ids.push_back(cur.Id);
		new_by_id[cur.Id] = orig;

		++row;
	}

	// Whatever is left without a partner either becomes the tail of the
	// original file (when opening) or is deleted along with its line
	if (initial) {
		for (auto const& pair : by_id)
			unpaired.push_back(pair.second);
	}
	else {
		for (auto const& pair : by_id) {
			if (claimed.count(pair.second))
				continue;

			if (keep_orphans) {
				unpaired.push_back(pair.second);
				continue;
			}

			removed[pair.first] = pair.second->Text.get();
			to_delete.push_back(pair.second);
		}
	}

	while (removed.size() > max_remembered_deleted)
		removed.erase(removed.begin());

	lines = std::move(new_lines);
	line_ids = std::move(new_ids);
	by_id = std::move(new_by_id);

	all_lines = lines;
	all_lines.insert(all_lines.end(), unpaired.begin(), unpaired.end());

	// Rebuild the file's line list in the new order
	while (!file->Events.empty())
		file->Events.erase(file->Events.begin());
	for (auto *line : lines)
		file->Events.push_back(*line);
	for (auto *line : unpaired)
		file->Events.push_back(*line);

	int new_row = 0;
	for (auto *line : lines)
		line->Row = new_row++;
	for (auto *line : unpaired)
		line->Row = new_row++;

	for (auto *line : to_delete)
		delete line;

	if (active_row >= (int)lines.size())
		active_row = lines.empty() ? 0 : (int)lines.size() - 1;

	syncing = false;

	UpdateDirection();
	AnnounceChange();
}

void OriginalSubsController::SplitFollowCurrent(
	std::vector<AssDialogue*> const& old_lines,
	std::vector<int> const& old_ids,
	std::unordered_map<int, int> const& current_rows,
	std::unordered_set<AssDialogue*> &claimed,
	std::unordered_map<int, AssDialogue*> &pieces)
{
	auto const& current = context->ass->Events;

	// The lines the current file had before this sync
	std::unordered_set<int> previous_ids(old_ids.begin(), old_ids.end());

	// The lines of the current file which did not exist before this sync, in
	// the order they appear in
	struct NewLine {
		AssDialogue const *line;
		int row;
	};
	std::vector<NewLine> new_lines;
	{
		int row = 0;
		for (auto const& cur : current) {
			if (!previous_ids.count(cur.Id))
				new_lines.push_back({&cur, row});
			++row;
		}
	}

	if (new_lines.size() < 2)
		return;

	// Rows of the lines of the current file which are already taken
	std::unordered_set<int> taken_rows;

	// Timestamps are stored with a resolution of a millisecond but compared
	// with a centisecond one, so allow for the rounding
	constexpr int tolerance = 20;

	for (size_t i = 0; i < old_lines.size() && i < old_ids.size(); ++i) {
		// A line which is still there has not been split
		if (current_rows.count(old_ids[i]))
			continue;

		AssDialogue *parent = old_lines[i];
		if (!parent || claimed.count(parent) || parent->Text.get() == placeholder_text)
			continue;

		int parent_start = (int)parent->Start;
		int parent_end = (int)parent->End;

		// The new lines of the current file which were carved out of the time
		// range of this one, which are the pieces it was split into
		std::vector<AssDialogue const*> found;
		std::vector<int> found_rows;
		int last_row = -1;
		for (auto const& candidate : new_lines) {
			int cur_row = candidate.row;
			auto const& cur = *candidate.line;

			if (taken_rows.count(cur_row)) {
				continue;
			}
			if ((int)cur.Start < parent_start - tolerance || (int)cur.End > parent_end + tolerance) {
				continue;
			}
			if (!found.empty() && cur_row != last_row + 1) {
				// Pieces have to be next to each other
				continue;
			}

			found.push_back(&cur);
			found_rows.push_back(cur_row);
			last_row = cur_row;
		}

		if (found.size() < 2)
			continue;

		// The pieces have to add up to most of the line they came from, or
		// this is a new line which merely happens to be timed inside this one
		int piece_start = (int)found.front()->Start;
		int piece_end = (int)found.back()->End;
		for (auto *cur : found) {
			piece_start = std::min(piece_start, (int)cur->Start);
			piece_end = std::max(piece_end, (int)cur->End);
		}
		if (piece_end - piece_start < (parent_end - parent_start) / 2)
			continue;

		auto texts = SplitText(parent->Text.get(), found.size());
		if (texts.size() != found.size())
			continue;

		for (size_t j = 0; j < found.size(); ++j) {
			AssDialogue *piece = parent;
			if (j > 0) {
				piece = new AssDialogue;
				piece->Style = "Original";
			}

			piece->Text = EnsureNonEmpty(texts[j]);
			claimed.insert(piece);
			pieces[found[j]->Id] = piece;
		}

		taken_rows.insert(found_rows.begin(), found_rows.end());

		LOG_D("subs/original") << "Split original line " << i << " into "
			<< found.size() << " pieces to follow the current file";
	}
}

void OriginalSubsController::EnsureStyle() {
	auto *orig = context->ass->GetStyle("Original");

	// An existing Original style belongs to the user: it is a normal style
	// which can be edited in the style manager, so leave it alone apart from
	// making sure it puts the original at the top of the screen
	if (orig) {
		if (orig->alignment == 8)
			return;

		bool was_syncing = syncing;
		syncing = true;
		orig->alignment = 8;
		orig->UpdateData();
		context->ass->Commit(_("update the Original style"), AssFile::COMMIT_STYLES);
		syncing = was_syncing;
		return;
	}

	// Otherwise clone the current Default style, with the only difference
	// being the alignment at the top of the screen
	AssStyle style;
	if (auto *def = context->ass->GetStyle("Default"))
		style = *def;
	style.name = "Original";
	style.alignment = 8;
	style.UpdateData();

	context->ass->Styles.push_back(*new AssStyle(style));

	bool was_syncing = syncing;
	syncing = true;
	context->ass->Commit(_("create the Original style"), AssFile::COMMIT_STYLES);
	syncing = was_syncing;
}

void OriginalSubsController::EnsureStyleIn(AssFile &target) const {
	auto *orig = target.GetStyle("Original");
	if (orig) {
		if (orig->alignment != 8) {
			orig->alignment = 8;
			orig->UpdateData();
		}
		return;
	}

	AssStyle style;
	if (auto *def = target.GetStyle("Default"))
		style = *def;
	style.name = "Original";
	style.alignment = 8;
	style.UpdateData();
	target.Styles.push_back(*new AssStyle(style));
}

std::unique_ptr<AssFile> OriginalSubsController::MakePreviewFile() const {
	auto merged = std::make_unique<AssFile>(*context->ass);

	if (!file)
		return merged;

	// The current file's copy of the Original style is authoritative, as it is
	// the one the user edits in the style manager. It has to exist before the
	// lines are added, as the video preview replaces the style of any line
	// whose style is missing with Default, which would put the original back
	// at the bottom of the screen.
	EnsureStyleIn(*merged);

	// all_lines is what the pane shows: the paired lines in the order of the
	// current file, followed by any lines the current file has no partner for.
	// Using it rather than the file's own line list keeps the preview and the
	// pane showing exactly the same thing.
	for (auto const *line : all_lines)
		merged->Events.push_back(*new AssDialogue(*line));

	LOG_D("subs/original") << "Preview file has " << merged->Events.size()
		<< " lines and " << merged->Styles.size() << " styles";

	return merged;
}

void OriginalSubsController::AutoSave() const {
	if (!file || !modified || filename.empty())
		return;

	auto directory = filename.parent_path();
	if (directory.empty())
		return;

	try {
		AssFile copy(*file);
		EnsureStyleIn(copy);

		auto path = directory / agi::format("%s.%s.ORIGINAL.AUTOSAVE.ass",
			filename.stem().string(), agi::util::strftime("%Y-%m-%d-%H-%M-%S"));
		SubtitleFormat::GetWriter(path)->WriteFile(&copy, path, context->project->Timecodes());
	}
	catch (agi::Exception const& err) {
		LOG_E("subs/original") << "Failed to autosave the original subtitles: " << err.GetMessage();
	}
	catch (...) {
		LOG_E("subs/original") << "Failed to autosave the original subtitles";
	}
}

void OriginalSubsController::SetLineText(int row, std::string const& text, bool amend) {
	if (row < 0 || (size_t)row >= lines.size())
		return;

	AssDialogue *line = lines[row];
	int id = line_ids[row];

	// An empty line would be dropped from some formats when saving, so it is
	// stored with a zero-width space instead
	std::string new_text = EnsureNonEmpty(text);

	if (line->Text.get() == new_text)
		return;

	if (amend && !undo_stack.empty() && undo_stack.back().id == id)
		undo_stack.back().after = new_text;
	else
		undo_stack.push_back({id, line->Text.get(), new_text});

	// A new edit invalidates the redo history
	redo_stack.clear();

	line->Text = new_text;
	active_row = row;
	SetModified(true);

	if (rtl::HasStrongDirection(text))
		direction = rtl::Detect(text);

	context->videoController->ReloadSubtitles();
	AnnounceChange();
}

void OriginalSubsController::Undo() {
	if (undo_stack.empty())
		return;

	TextEdit edit = undo_stack.back();
	undo_stack.pop_back();

	if (auto *line = LineById(edit.id))
		line->Text = EnsureNonEmpty(edit.before);

	redo_stack.push_back(edit);
	SetModified(true);

	context->videoController->ReloadSubtitles();
	AnnounceChange();
}

void OriginalSubsController::Redo() {
	if (redo_stack.empty())
		return;

	TextEdit edit = redo_stack.back();
	redo_stack.pop_back();

	if (auto *line = LineById(edit.id))
		line->Text = EnsureNonEmpty(edit.after);

	undo_stack.push_back(edit);
	SetModified(true);

	context->videoController->ReloadSubtitles();
	AnnounceChange();
}
