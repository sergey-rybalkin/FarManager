﻿#ifndef FILEMASKS_HPP_80DC5089_9F1B_484C_BC52_39A6AA1C7299
#define FILEMASKS_HPP_80DC5089_9F1B_484C_BC52_39A6AA1C7299
#pragma once

/*
filemasks.hpp

Класс для работы с масками файлов (учитывается наличие масок исключения).
*/
/*
Copyright © 1996 Eugene Roshal
Copyright © 2000 Far Group
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

// Internal:

// Platform:

// Common:
#include "common/preprocessor.hpp"
#include "common/string_utils.hpp"

// External:

//----------------------------------------------------------------------------

struct RegExpMatch;

enum FM_FLAGS
{
	FMF_SILENT = 1,
};

class filemasks
{
public:
	NONCOPYABLE(filemasks);

	filemasks();
	~filemasks();

	filemasks(filemasks&&) noexcept;
	filemasks& operator=(filemasks&&) noexcept;

	bool assign(string_view Str, DWORD Flags = 0);
	using regex_matches = std::pair<std::vector<RegExpMatch>&, unordered_string_map<size_t>&>;
	bool check(string_view Name, regex_matches const* Matches = {}) const;
	bool empty() const;

	static void ErrorMessage();

private:
	class masks;

	void clear();

	std::vector<masks> Include, Exclude;
};

#endif // FILEMASKS_HPP_80DC5089_9F1B_484C_BC52_39A6AA1C7299
