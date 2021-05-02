﻿/*
colormix.cpp

Работа с цветами
*/
/*
Copyright © 2011 Far Group
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the authors may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// BUGBUG
#include "platform.headers.hpp"

// Self:
#include "colormix.hpp"

// Internal:
#include "config.hpp"
#include "console.hpp"
#include "global.hpp"

// Platform:

// Common:
#include "common/from_string.hpp"

// External:

//----------------------------------------------------------------------------

enum
{
	ConsoleMask=0xf,
	ConsoleBgShift=4,
	ConsoleFgShift=0,
};

namespace colors
{
	COLORREF index_value(COLORREF const Colour)
	{
		return Colour & INDEXMASK;
	}

	COLORREF color_value(COLORREF const Colour)
	{
		return Colour & COLORMASK;
	}

	COLORREF alpha_value(COLORREF const Colour)
	{
		return Colour & ALPHAMASK;
	}

	bool is_opaque(COLORREF const Colour)
	{
		return alpha_value(Colour) == ALPHAMASK;
	}

	bool is_transparent(COLORREF const Colour)
	{
		return !alpha_value(Colour);
	}

	COLORREF opaque(COLORREF const Colour)
	{
		return Colour | ALPHAMASK;
	}

	COLORREF transparent(COLORREF const Colour)
	{
		return Colour & COLORMASK;
	}

	void make_opaque(COLORREF& Colour)
	{
		Colour = opaque(Colour);
	}

	void make_transparent(COLORREF& Colour)
	{
		Colour = transparent(Colour);
	}

	size_t color_hash(const FarColor& Value)
	{
		return hash_combine_all(
			Value.Flags,
			Value.BackgroundColor,
			Value.ForegroundColor);
	}

	FarColor merge(const FarColor& Bottom, const FarColor& Top)
	{
		auto Result = Bottom;

		const auto merge_part = [&](COLORREF FarColor::*ColorAccessor, const FARCOLORFLAGS Flag)
		{
			const auto TopValue = std::invoke(ColorAccessor, Top);
			if (is_transparent(TopValue))
				return;

			// TODO: proper alpha blending?
			std::invoke(ColorAccessor, Result) = TopValue;

			flags::copy(Result.Flags, Flag, Top.Flags);
		};

		merge_part(&FarColor::BackgroundColor, FCF_BG_4BIT);
		merge_part(&FarColor::ForegroundColor, FCF_FG_4BIT);

		if (!(Top.Flags & FCF_IGNORE_STYLE))
			flags::copy(Result.Flags, FCF_STYLEMASK, Top.Flags);

		return Result;
	}

	static auto console_palette()
	{
		std::array<COLORREF, 16> Palette;
		if (console.GetPalette(Palette))
			return Palette;

		return std::array
		{
			RGB(  0,   0,   0), // black
			RGB(  0,   0, 128), // blue
			RGB(  0, 128,   0), // green
			RGB(  0, 128, 128), // cyan
			RGB(128,   0,   0), // red
			RGB(128,   0, 128), // magenta
			RGB(128, 128,   0), // yellow
			RGB(192, 192, 192), // white

			RGB(128, 128, 128), // bright black
			RGB(  0,   0, 255), // bright blue
			RGB(  0, 255,   0), // bright green
			RGB(  0, 255, 255), // bright cyan
			RGB(255,   0,   0), // bright red
			RGB(255,   0, 255), // bright magenta
			RGB(255, 255,   0), // bright yellow
			RGB(255, 255, 255)  // bright white
		};
	}

	static WORD get_closest_palette_index(COLORREF const Color)
	{
		static const auto Palette = console_palette();

		static std::unordered_map<COLORREF, WORD> Map;

		if (const auto Iterator = Map.find(Color); Iterator != Map.cend())
			return Iterator->second;

		union color_point
		{
			const COLORREF Color;
			const rgba RGBA;
		};

		color_point const Point{ Color };

		const auto distance = [&](COLORREF const PaletteColor)
		{
			color_point const PalettePoint{ PaletteColor };

			const auto distance_part = [&](unsigned char rgba::* const Getter)
			{
				return std::abs(
					int{ std::invoke(Getter, Point.RGBA) } -
					int{ std::invoke(Getter, PalettePoint.RGBA) }
				);
			};

			return std::sqrt(
				std::pow(distance_part(&rgba::r), 2) +
				std::pow(distance_part(&rgba::g), 2) +
				std::pow(distance_part(&rgba::b), 2)
			);
		};

		const auto ClosestPointIterator = std::min_element(ALL_CONST_RANGE(Palette), [&](COLORREF const Item1, COLORREF const Item2)
		{
			return distance(Item1) < distance(Item2);
		});

		const auto ClosestIndex = ClosestPointIterator - Palette.cbegin();

		Map.emplace(Color, ClosestIndex);

		return ClosestIndex;
	}


WORD FarColorToConsoleColor(const FarColor& Color)
{
	static FarColor LastColor{};
	static WORD Result = 0;

	const auto NonColorAttributes =
		(Color.Flags & FCF_RAWATTR_MASK) |
		(Color.Flags & FCF_FG_UNDERLINE? COMMON_LVB_UNDERSCORE : 0) |
		(Color.Flags & FCF_FG_OVERLINE? COMMON_LVB_GRID_HORIZONTAL : 0);

	if (
		Color.BackgroundColor == LastColor.BackgroundColor &&
		Color.ForegroundColor == LastColor.ForegroundColor &&
		(Color.Flags & FCF_4BITMASK) == (LastColor.Flags & FCF_4BITMASK)
	)
		return Result | NonColorAttributes;

	const auto convert_and_save = [&](COLORREF FarColor::* const Getter, FARCOLORFLAGS const Flag, BYTE& IndexColor)
	{
		const auto Current = std::invoke(Getter, Color);
		auto& Last = std::invoke(Getter, LastColor);

		if (Current == Last)
			return;

		Last = Current;

		if (Color.Flags & Flag)
		{
			IndexColor = Current & ConsoleMask;
			return;
		}

		IndexColor = get_closest_palette_index(color_value(Current));
	};

	static struct
	{
		uint8_t
			Foreground,
			Background;
	}
	Index{};

	convert_and_save(&FarColor::ForegroundColor, FCF_FG_4BIT, Index.Foreground);
	convert_and_save(&FarColor::BackgroundColor, FCF_BG_4BIT, Index.Background);

	LastColor.Flags = Color.Flags;

	auto FinalIndex = Index;

	if (
		FinalIndex.Foreground == FinalIndex.Background &&
		color_value(Color.ForegroundColor) != color_value(Color.BackgroundColor)
	)
	{
		// oops, unreadable
		// since background is more pronounced we adjust the foreground only
		flags::invert(FinalIndex.Foreground, FOREGROUND_INTENSITY);
	}

	Result =
		(FinalIndex.Foreground << ConsoleFgShift) |
		(FinalIndex.Background << ConsoleBgShift);

	return Result | NonColorAttributes;
}

FarColor ConsoleColorToFarColor(WORD Color)
{
	return
	{
		FCF_FG_4BIT | FCF_BG_4BIT | FCF_IGNORE_STYLE | (Color & FCF_RAWATTR_MASK),
		{ opaque((Color >> ConsoleFgShift) & ConsoleMask) },
		{ opaque((Color >> ConsoleBgShift) & ConsoleMask) }
	};
}

COLORREF ConsoleIndexToTrueColor(size_t Index)
{
	return opaque(console_palette()[Index & ConsoleMask]);
}

const FarColor& PaletteColorToFarColor(PaletteColors ColorIndex)
{
	return Global->Opt->Palette[ColorIndex];
}

const FarColor* StoreColor(const FarColor& Value)
{
	static std::unordered_set<FarColor> ColorSet;
	return &*ColorSet.emplace(Value).first;
}

COLORREF ARGB2ABGR(int Color)
{
	return (Color & 0xFF00FF00) | ((Color & 0x00FF0000) >> 16) | ((Color & 0x000000FF) << 16);
}

static bool ExtractColor(string_view const Str, COLORREF& Target, FARCOLORFLAGS& TargetFlags, FARCOLORFLAGS SetFlag)
{
	const auto IsTrueColour = Str.front() == L'T';

	if (!from_string(Str.substr(IsTrueColour? 1 : 0), Target, nullptr, 16))
		return false;

	if (IsTrueColour)
	{
		Target = ARGB2ABGR(Target);
		TargetFlags &= ~SetFlag;
	}
	else
	{
		TargetFlags |= SetFlag;
	}

	return true;
}

string_view ExtractColorInNewFormat(string_view const Str, FarColor& Color, bool& Stop)
{
	Stop = false;

	if (!starts_with(Str, L'('))
		return Str;

	const auto FgColorBegin = Str.cbegin() + 1;
	const auto ColorEnd = std::find(FgColorBegin, Str.cend(), L')');
	if (ColorEnd == Str.cend())
	{
		Stop = true;
		return Str;
	}

	const auto FgColorEnd = std::find(FgColorBegin, ColorEnd, L':');
	const auto BgColorBegin = FgColorEnd == ColorEnd? ColorEnd : FgColorEnd + 1;
	const auto BgColorEnd = ColorEnd;

	auto NewColor = Color;
	if (
		(FgColorBegin == FgColorEnd || ExtractColor(make_string_view(FgColorBegin, FgColorEnd), NewColor.ForegroundColor, NewColor.Flags, FCF_FG_4BIT)) &&
		(BgColorBegin == BgColorEnd || ExtractColor(make_string_view(BgColorBegin, BgColorEnd), NewColor.BackgroundColor, NewColor.Flags, FCF_BG_4BIT))
	)
	{
		Color = NewColor;
		return make_string_view(ColorEnd + 1, Str.cend());
	}

	return Str;
}

}

#ifdef ENABLE_TESTS

#include "testing.hpp"

TEST_CASE("colors.COLORREF")
{
	static const struct
	{
		COLORREF Src, Alpha, Color, ABGR, Index;
		bool Opaque, Transparent;

	}
	Tests[]
	{
		{ 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00, false, true  },
		{ 0x00000001, 0x00000000, 0x00000001, 0x00010000, 0x01, false, true  },
		{ 0x01000000, 0x01000000, 0x00000000, 0x01000000, 0x00, false, false },
		{ 0xFF000000, 0xFF000000, 0x00000000, 0xFF000000, 0x00, true,  false },
		{ 0x00ABCDEF, 0x00000000, 0x00ABCDEF, 0x00EFCDAB, 0x0F, false, true  },
		{ 0xFFFFFFFF, 0xFF000000, 0x00FFFFFF, 0xFFFFFFFF, 0x0F, true,  false },
	};

	for (const auto& i: Tests)
	{
		REQUIRE(colors::alpha_value(i.Src) == i.Alpha);
		REQUIRE(colors::color_value(i.Src) == i.Color);
		REQUIRE(colors::index_value(i.Src) == i.Index);
		REQUIRE(colors::is_opaque(i.Src) == i.Opaque);
		REQUIRE(colors::is_transparent(i.Src) == i.Transparent);
		REQUIRE(colors::is_opaque(colors::opaque(i.Src)));
		REQUIRE(colors::is_transparent(colors::transparent(i.Src)));
		REQUIRE(colors::ARGB2ABGR(i.Src) == i.ABGR);
	}
}

TEST_CASE("colors.parser")
{
	static const struct
	{
		string_view Input;
		FarColor Color;
	}
	ValidTests[]
	{
		{ L"(E)"sv,               { FCF_FG_4BIT, {0xE}, {0} } },
		{ L"(:F)"sv,              { FCF_BG_4BIT, {0}, {0xF} } },
		{ L"(B:C)"sv,             { FCF_FG_4BIT | FCF_BG_4BIT, {0xB}, {0xC} } },
		{ L"()"sv,                { } },
		{ L"(T00CCCC:TE34234)"sv, { 0, {0x00CCCC00}, {0x003442E3} } },
	};

	for (const auto& i: ValidTests)
	{
		FarColor Color{};
		bool Stop = false;
		const auto Tail = colors::ExtractColorInNewFormat(i.Input, Color, Stop);
		REQUIRE(Color == i.Color);
		REQUIRE(Tail.empty());
		REQUIRE(!Stop);
	}

	static const struct
	{
		string_view Input;
		bool Stop;
	}
	InvalidTests[]
	{
		{ {},            false },
		{ L"("sv,        true  },
		{ L"(z"sv,       true  },
		{ L"(z)"sv,      false },
		{ L"(0:z)"sv,    false },
		{ L"(z:0)"sv,    false },
		{ L"(Tz)"sv,     false },
		{ L"( )"sv,      false },
		{ L"( 0)"sv,     false },
		{ L"( -0)"sv,    false },
		{ L"( +0)"sv,    false },
	};

	for (const auto& i: InvalidTests)
	{
		FarColor Color{};
		bool Stop = false;
		const auto Tail = colors::ExtractColorInNewFormat(i.Input, Color, Stop);
		REQUIRE(Tail.size() == i.Input.size());
		REQUIRE(Stop == i.Stop);
	}
}
#endif
