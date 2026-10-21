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

#include <libaegisub/ass/text_direction.h>

namespace {

struct Counts {
	int rtl = 0;
	int ltr = 0;

	bool IsRTL() const { return rtl > ltr; }
};

/// Decode one UTF-8 code point, advancing pos past it
unsigned decode_utf8(std::string_view str, size_t &pos) {
	unsigned char c = str[pos];
	if (c < 0x80) {
		++pos;
		return c;
	}

	int extra;
	unsigned cp;
	if ((c & 0xE0) == 0xC0)      { cp = c & 0x1F; extra = 1; }
	else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
	else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
	else { ++pos; return 0xFFFD; }

	if (pos + extra >= str.size()) {
		pos = str.size();
		return 0xFFFD;
	}

	for (int i = 1; i <= extra; ++i) {
		unsigned char cc = str[pos + i];
		if ((cc & 0xC0) != 0x80) {
			pos += i;
			return 0xFFFD;
		}
		cp = (cp << 6) | (cc & 0x3F);
	}
	pos += extra + 1;
	return cp;
}

bool is_rtl(unsigned cp) {
	return (cp >= 0x0590 && cp <= 0x05FF) ||  // Hebrew
	       (cp >= 0x0600 && cp <= 0x06FF) ||  // Arabic (incl. Persian)
	       (cp >= 0x0700 && cp <= 0x074F) ||  // Syriac
	       (cp >= 0x0750 && cp <= 0x077F) ||  // Arabic Supplement
	       (cp >= 0x0780 && cp <= 0x07BF) ||  // Thaana
	       (cp >= 0x07C0 && cp <= 0x07FF) ||  // NKo
	       (cp >= 0x0800 && cp <= 0x08FF) ||  // Samaritan / Mandaic / Arabic Ext-A
	       (cp >= 0xFB1D && cp <= 0xFDFF) ||  // Hebrew and Arabic presentation forms
	       (cp >= 0xFE70 && cp <= 0xFEFF);    // Arabic presentation forms-B
}

bool is_ltr(unsigned cp) {
	return (cp >= 0x0041 && cp <= 0x005A) ||  // A-Z
	       (cp >= 0x0061 && cp <= 0x007A) ||  // a-z
	       (cp >= 0x00C0 && cp <= 0x024F) ||  // Latin-1 supplement and extended
	       (cp >= 0x0370 && cp <= 0x03FF) ||  // Greek
	       (cp >= 0x0400 && cp <= 0x052F) ||  // Cyrillic
	       (cp >= 0x0530 && cp <= 0x058F) ||  // Armenian
	       (cp >= 0x0900 && cp <= 0x0DFF) ||  // Indic
	       (cp >= 0x10A0 && cp <= 0x10FF) ||  // Georgian
	       (cp >= 0x1E00 && cp <= 0x1EFF) ||  // Latin extended additional
	       (cp >= 0x3040 && cp <= 0x30FF) ||  // Kana
	       (cp >= 0x4E00 && cp <= 0x9FFF) ||  // CJK
	       (cp >= 0xAC00 && cp <= 0xD7AF) ||  // Hangul
	       (cp >= 0xFF21 && cp <= 0xFF3A) ||  // Fullwidth A-Z
	       (cp >= 0xFF41 && cp <= 0xFF5A);    // Fullwidth a-z
}

bool is_neutral_digit_or_punct(unsigned cp) {
	// Digits are direction-neutral (AN/EN in the Unicode bidi algorithm).
	return cp < 0x0041 ||
	       (cp >= 0x0030 && cp <= 0x0039) ||
	       (cp >= 0x0660 && cp <= 0x0669) ||  // Arabic-Indic digits
	       (cp >= 0x06F0 && cp <= 0x06F9);    // Extended Arabic-Indic (Persian) digits
}

Counts count(std::string_view text, bool skip_tag_blocks) {
	Counts ret;

	size_t pos = 0;
	while (pos < text.size()) {
		if (skip_tag_blocks && text[pos] == '{') {
			auto end = text.find('}', pos);
			if (end == std::string_view::npos)
				break;
			pos = end + 1;
			continue;
		}

		unsigned cp = decode_utf8(text, pos);
		if (cp == 0xFFFD || is_neutral_digit_or_punct(cp))
			continue;

		if (is_rtl(cp))
			++ret.rtl;
		else if (is_ltr(cp))
			++ret.ltr;
	}

	return ret;
}

} // namespace

namespace agi::ass {

TextDirection DetectTextDirection(std::string_view text) {
	return count(text, true).IsRTL() ? TextDirection::RTL : TextDirection::LTR;
}

bool HasStrongDirection(std::string_view text) {
	auto counts = count(text, true);
	return counts.rtl > 0 || counts.ltr > 0;
}

std::string ToDisplayText(std::string_view text, bool strip_override_blocks, bool line_breaks_as_newlines) {
	std::string ret;
	ret.reserve(text.size());

	for (size_t i = 0; i < text.size(); ++i) {
		if (strip_override_blocks && text[i] == '{') {
			auto end = text.find('}', i);
			if (end != std::string_view::npos) {
				i = end;
				continue;
			}
		}

		if (text[i] == '\\' && i + 1 < text.size()) {
			char next = text[i + 1];
			if (next == 'N' || next == 'n') {
				ret += line_breaks_as_newlines ? '\n' : ' ';
				++i;
				continue;
			}
			if (next == 'h') {
				ret += ' ';
				++i;
				continue;
			}
		}

		ret += text[i];
	}

	return ret;
}

} // namespace agi::ass
