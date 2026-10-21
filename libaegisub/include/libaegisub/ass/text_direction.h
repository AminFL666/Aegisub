// Copyright (c) 2026, Aegisub Original Subtitle contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// Aegisub Project http://www.aegisub.org/

/// @file text_direction.h
/// @brief Detection of the direction of subtitle text
/// @ingroup ass

#pragma once

#include <string>
#include <string_view>

namespace agi::ass {

/// Direction of a piece of text
enum class TextDirection {
	LTR,
	RTL
};

/// @brief Detect the dominant direction of a subtitle line
///
/// Override blocks ({\...}) are ignored, so that a line made up mostly of tags
/// and a little Farsi still counts as RTL. Digits, punctuation and whitespace
/// are direction-neutral and do not influence the result.
/// @param text Raw ASS line text
/// @return Dominant direction, or LTR if the text has no strong characters
TextDirection DetectTextDirection(std::string_view text);

/// @brief Does the text contain any strongly directional characters?
///
/// Text with no letters at all (an empty line, or one made up only of numbers
/// and punctuation) has no direction of its own and should inherit the
/// direction of the file it belongs to.
bool HasStrongDirection(std::string_view text);

/// @brief Replace line break and hard space tags with the characters they stand for
/// @param text Raw ASS line text
/// @param strip_override_blocks Remove {\...} blocks
/// @param line_breaks_as_newlines Use newlines rather than spaces for \N and \n
std::string ToDisplayText(std::string_view text, bool strip_override_blocks = true,
	bool line_breaks_as_newlines = true);

} // namespace agi::ass
