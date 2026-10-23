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
	reader->ReadFile(new_file.get(), path, context->project->Timecodes(), encoding.c_str());

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
		out.WriteLineToFile(rtl::ToPlainText(line.Text.get()));
		out.WriteLineToFile("");
	}
}

void OriginalSubsController::SyncToCurrentFile(bool initial) {
	if (!file || syncing)
		return;

	syncing = true;

	std::vector<AssDialogue*> new_lines;
	std::vector<int> new_ids;
	std::unordered_map<int, AssDialogue*> new_by_id;
	std::vector<AssDialogue*> to_delete;

	new_lines.reserve(context->ass->Events.size());
	new_ids.reserve(context->ass->Events.size());

	for (auto const& cur : context->ass->Events) {
		AssDialogue *orig = nullptr;

		auto paired = by_id.find(cur.Id);
		if (paired != by_id.end()) {
			orig = paired->second;
			by_id.erase(paired);
		}
		else {
			auto resurrected = removed.find(cur.Id);
			if (resurrected != removed.end()) {
				// The line was deleted and is back, most likely because the
				// deletion was undone, so restore its text
				orig = new AssDialogue;
				orig->Text = resurrected->second;
				removed.erase(resurrected);
			}
			else if (!unpaired.empty()) {
				// The original has more lines than the current file had before
				orig = unpaired.front();
				unpaired.erase(unpaired.begin());
			}
			else {
				orig = new AssDialogue;
				orig->Text = placeholder_text;
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
	}

	// Whatever is left without a partner either becomes the tail of the
	// original file (when opening) or is deleted along with its line
	if (initial) {
		for (auto const& pair : by_id)
			unpaired.push_back(pair.second);
	}
	else {
		for (auto const& pair : by_id) {
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

	int row = 0;
	for (auto *line : lines)
		line->Row = row++;
	for (auto *line : unpaired)
		line->Row = row++;

	for (auto *line : to_delete)
		delete line;

	if (active_row >= (int)lines.size())
		active_row = lines.empty() ? 0 : (int)lines.size() - 1;

	syncing = false;

	UpdateDirection();
	AnnounceChange();
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
	// the one the user edits in the style manager
	EnsureStyleIn(*merged);

	for (auto const& line : file->Events)
		merged->Events.push_back(*new AssDialogue(line));

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

	if (line->Text.get() == text)
		return;

	if (amend && !undo_stack.empty() && undo_stack.back().id == id)
		undo_stack.back().after = text;
	else
		undo_stack.push_back({id, line->Text.get(), text});

	// A new edit invalidates the redo history
	redo_stack.clear();

	line->Text = text;
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
		line->Text = edit.before;

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
		line->Text = edit.after;

	undo_stack.push_back(edit);
	SetModified(true);

	context->videoController->ReloadSubtitles();
	AnnounceChange();
}
