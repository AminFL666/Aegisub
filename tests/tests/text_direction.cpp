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

#include <main.h>

using namespace agi::ass;

TEST(text_direction, farsi_is_rtl) {
	EXPECT_EQ(TextDirection::RTL, DetectTextDirection("\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 \xd8\xaf\xd9\x86\xdb\x8c\xd8\xa7"));
}

TEST(text_direction, latin_is_ltr) {
	EXPECT_EQ(TextDirection::LTR, DetectTextDirection("Hello world"));
}

TEST(text_direction, hebrew_is_rtl) {
	EXPECT_EQ(TextDirection::RTL, DetectTextDirection("\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d"));
}

TEST(text_direction, override_blocks_are_ignored) {
	EXPECT_EQ(TextDirection::RTL, DetectTextDirection("{\\an8}\\N\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85"));
	EXPECT_EQ(TextDirection::LTR, DetectTextDirection("{\\an8}\\NHello"));
}

TEST(text_direction, neutral_text_has_no_direction) {
	EXPECT_FALSE(HasStrongDirection(""));
	EXPECT_FALSE(HasStrongDirection("123 - 456"));
	EXPECT_TRUE(HasStrongDirection("hi"));
	EXPECT_TRUE(HasStrongDirection("\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85"));
}

TEST(text_direction, latin_wins_over_a_little_farsi) {
	EXPECT_EQ(TextDirection::LTR, DetectTextDirection("This is mostly English with one word: \xd8\xb3\xd9\x84\xd8\xa7\xd9\x85"));
}

TEST(text_direction, display_text) {
	// Override blocks are removed and line breaks become newlines
	EXPECT_EQ("\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85\n\xd8\xaf\xd9\x86\xdb\x8c\xd8\xa7",
		ToDisplayText("{\\i1}\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85\\N\xd8\xaf\xd9\x86\xdb\x8c\xd8\xa7"));
	EXPECT_EQ("a b", ToDisplayText("a\\Nb", true, false));
	EXPECT_EQ("{\\i1}a b", ToDisplayText("{\\i1}a\\Nb", false, false));
	EXPECT_EQ("a b", ToDisplayText("a\\hb"));
}
