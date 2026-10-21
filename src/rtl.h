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

/// @file rtl.h
/// @brief Right-to-left text handling used by the subtitle views
/// @ingroup subs_storage

#pragma once

#include <libaegisub/ass/text_direction.h>

#include <string>
#include <string_view>

class AssFile;

/// Right-to-left support for the subtitle grids and the text editor
namespace rtl {

using Direction = agi::ass::TextDirection;

/// @brief Detect the dominant direction of a subtitle line
inline Direction Detect(std::string_view text) { return agi::ass::DetectTextDirection(text); }

/// @brief Does the text contain any strongly directional characters?
inline bool HasStrongDirection(std::string_view text) { return agi::ass::HasStrongDirection(text); }

/// @brief Is the text right to left?
inline bool IsRTL(std::string_view text) { return Detect(text) == Direction::RTL; }

/// @brief Detect the dominant direction of an entire file
///
/// Used to pick the direction for lines which have no direction of their own,
/// such as empty or numeric-only lines.
Direction DetectMajority(AssFile const& file);

/// @brief Strip override blocks and turn line break tags into newlines or spaces
inline std::string ToPlainText(std::string_view text, bool keep_line_breaks = true) {
	return agi::ass::ToDisplayText(text, true, keep_line_breaks);
}

} // namespace rtl
