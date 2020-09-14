﻿/*
exception_handler.cpp

*/
/*
Copyright © 2018 Far Group
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

// Self:
#include "exception_handler.hpp"

// Internal:
#include "plugins.hpp"
#include "filepanels.hpp"
#include "manager.hpp"
#include "config.hpp"
#include "dialog.hpp"
#include "farcolor.hpp"
#include "colormix.hpp"
#include "interf.hpp"
#include "keys.hpp"
#include "keyboard.hpp"
#include "lang.hpp"
#include "language.hpp"
#include "message.hpp"
#include "imports.hpp"
#include "strmix.hpp"
#include "tracer.hpp"
#include "pathmix.hpp"
#include "global.hpp"
#include "farversion.hpp"
#include "clipboard.hpp"
#include "eol.hpp"

// Platform:
#include "platform.fs.hpp"
#include "platform.reg.hpp"
#include "platform.version.hpp"

// Common:
#include "common/view/zip.hpp"

// External:
#include "format.hpp"

//----------------------------------------------------------------------------

namespace detail
{
	static bool HandleCppExceptions = true;
	static bool HandleSehExceptions = true;

	static std::atomic_bool UseTerminateHandler = false;
}

void disable_exception_handling()
{
	detail::HandleCppExceptions = false;
	detail::HandleSehExceptions = false;
}

void CreatePluginStartupInfo(PluginStartupInfo *PSI, FarStandardFunctions *FSF);

constexpr inline NTSTATUS EXCEPTION_MICROSOFT_CPLUSPLUS = 0xE06D7363;

enum exception_dialog
{
	ed_doublebox,
	ed_first_line,
	ed_text_exception = ed_first_line,
	ed_edit_exception,
	ed_text_details,
	ed_edit_details,
	ed_text_errno,
	ed_edit_errno,
	ed_text_lasterror,
	ed_edit_lasterror,
	ed_text_ntstatus,
	ed_edit_ntstatus,
	ed_text_address,
	ed_edit_address,
	ed_text_function,
	ed_edit_function,
	ed_text_source,
	ed_edit_source,
	ed_text_module,
	ed_edit_module,
	ed_text_plugin_information,
	ed_edit_plugin_information,
	ed_text_far_version,
	ed_edit_far_version,
	ed_text_os_version,
	ed_edit_os_version,
	ed_text_kernel_version,
	ed_edit_kernel_version,
	ed_last_line = ed_edit_kernel_version,
	ed_separator_1,
	ed_button_copy,
	ed_button_stack,
	ed_button_minidump,
	ed_separator_2,
	ed_button_terminate,
	ed_button_unload,
	ed_button_ignore,

	ed_first_button = ed_button_copy,
	ed_last_button = ed_button_ignore,

	ed_items_count
};

static auto GetStackTrace(string_view const Module, const std::vector<DWORD64>& Stack, const std::vector<DWORD64>* NestedStack)
{
	std::vector<string> Symbols;
	Symbols.reserve(Stack.size() + (NestedStack? NestedStack->size() + 3 : 0));

	const auto Consumer = [&Symbols](string&& Address, string&& Name, string&& Source)
	{
		if (!Name.empty())
			append(Address, L' ', Name);

		if (!Source.empty())
			append(Address, L" ("sv, Source, L')');

		Symbols.emplace_back(std::move(Address));
	};

	if (NestedStack)
	{
		tracer::get_symbols(Module, *NestedStack, Consumer);

		Symbols.emplace_back(40, L'-');
		Symbols.emplace_back(L"Rethrow:"sv);
		Symbols.emplace_back(40, L'-');
	}

	tracer::get_symbols(Module, Stack, Consumer);

	return Symbols;
}

static void ShowStackTrace(std::vector<string> const& Symbols)
{
	if (Global && Global->WindowManager && !Global->WindowManager->ManagerIsDown())
	{
		Message(MSG_WARNING | MSG_LEFTALIGN,
			msg(lng::MExcTrappedException),
			std::move(Symbols),
			{ lng::MOk });
	}
	else
	{
		std::wcerr << L'\n';

		for (const auto& Str: Symbols)
		{
			std::wcerr << Str << L'\n';
		}
	}
}

static bool write_minidump(const detail::exception_context& Context, string_view const Path)
{
	if (!imports.MiniDumpWriteDump)
		return false;

	const os::fs::file DumpFile(Path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS);
	if (!DumpFile)
		return false;

	MINIDUMP_EXCEPTION_INFORMATION Mei = { Context.thread_id(), Context.pointers() };
	return imports.MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), DumpFile.get().native_handle(), MiniDumpWithFullMemory, &Mei, nullptr, nullptr) != FALSE;
}

static string self_version()
{
	const auto Version = format(FSTR(L"{0} {1}"), version_to_string(build::version()), build::platform());
	const auto ScmRevision = build::scm_revision();
	return ScmRevision.empty()? Version : Version + format(FSTR(L" ({0:.7})"), ScmRevision);
}

static bool get_os_version(OSVERSIONINFOEX& Info)
{
	const auto InfoPtr = static_cast<OSVERSIONINFO*>(static_cast<void*>(&Info));

	if (imports.RtlGetVersion && imports.RtlGetVersion(InfoPtr) == STATUS_SUCCESS)
		return true;

WARNING_PUSH()
WARNING_DISABLE_MSC(4996) // 'GetVersionExW': was declared deprecated. So helpful. :(
WARNING_DISABLE_CLANG("-Wdeprecated-declarations")
	return GetVersionEx(InfoPtr) != FALSE;
WARNING_POP()
}

static string os_version_from_api()
{
	OSVERSIONINFOEX Info{ sizeof(Info) };
	if (!get_os_version(Info))
		return L"Unknown"s;

	return format(FSTR(L"{0}.{1}.{2}.{3}.{4}.{5}.{6}.{7}"),
		Info.dwMajorVersion,
		Info.dwMinorVersion,
		Info.dwBuildNumber,
		Info.dwPlatformId,
		Info.wServicePackMajor,
		Info.wServicePackMinor,
		Info.wSuiteMask,
		Info.wProductType
	);
}

// Mental OS - mental methods *facepalm*
static string os_version_from_registry()
{
	const auto Key = os::reg::key::open(os::reg::key::local_machine, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"sv, KEY_QUERY_VALUE);
	if (!Key)
		return {};

	string ReleaseId, CurrentBuild;
	unsigned UBR;
	if (!Key.get(L"ReleaseId"sv, ReleaseId) || !Key.get(L"CurrentBuild"sv, CurrentBuild) || !Key.get(L"UBR"sv, UBR))
		return {};

	return format(FSTR(L" (version {0}, OS build {1}.{2})"), ReleaseId, CurrentBuild, UBR);
}

static string os_version()
{
	return os_version_from_api() + os_version_from_registry();
}

static string kernel_version()
{
	os::version::file_version Version;
	if (!Version.read(L"ntoskrnl.exe"sv))
		return L"Unknown"s;

	if (const auto Str = Version.get_string(L"FileVersion"sv))
		return Str;

	const auto FixedInfo = Version.get_fixed_info();
	if (!FixedInfo)
		return L"Unknown"s;

	return format(FSTR(L"{0}.{1}.{2}.{3}"),
		HIWORD(FixedInfo->dwFileVersionMS),
		LOWORD(FixedInfo->dwFileVersionMS),
		HIWORD(FixedInfo->dwFileVersionLS),
		LOWORD(FixedInfo->dwFileVersionLS)
	);
}

struct dialog_data_type
{
	const detail::exception_context* Context;
	const std::vector<DWORD64>* NestedStack;
	string_view Module;
};

static void copy_information(Dialog* const Dlg)
{
	string Strings;
	const auto Eol = eol::system.str();
	FarDialogItem di;
	Dlg->SendMessage(DM_GETDLGITEMSHORT, 1, &di);
	const auto Width = di.X2 - di.X1 + 1;

	for (size_t i = ed_text_exception; i != ed_separator_1; ++i)
	{
		const auto Str = reinterpret_cast<const wchar_t*>(Dlg->SendMessage(DM_GETCONSTTEXTPTR, i, nullptr));
		append(Strings, format(FSTR(L"{0:{1}}{2}"), Str, i & 1 ? Width : 0, i & 1 ? L" "sv : Eol));
	}

	append(Strings, Eol);

	const auto& Data = *reinterpret_cast<dialog_data_type*>(Dlg->SendMessage(DM_GETDLGDATA, 0, nullptr));
	for (const auto& i: GetStackTrace(Data.Module, tracer::get(Data.Module, *Data.Context->pointers(), Data.Context->thread_handle()), Data.NestedStack))
	{
		append(Strings, i, Eol);
	}

	SetClipboardText(Strings);
}

static intptr_t ExcDlgProc(Dialog* Dlg,intptr_t Msg,intptr_t Param1,void* Param2)
{
	switch (Msg)
	{
		case DN_CTLCOLORDLGITEM:
		{
			FarDialogItem di;
			Dlg->SendMessage(DM_GETDLGITEMSHORT,Param1,&di);

			if (di.Type==DI_EDIT)
			{
				const auto& Color = colors::PaletteColorToFarColor(COL_WARNDIALOGTEXT);
				const auto Colors = static_cast<FarDialogItemColors*>(Param2);
				Colors->Colors[0] = Color;
				Colors->Colors[2] = Color;
			}
		}
		break;

		case DN_CONTROLINPUT:
		{
			const auto record = static_cast<const INPUT_RECORD *>(Param2);
			if (record->EventType == KEY_EVENT)
			{
				switch (InputRecordToKey(record))
				{
				case KEY_LEFT:
				case KEY_NUMPAD4:
				case KEY_SHIFTTAB:
					if (Param1 == ed_first_button)
					{
						Dlg->SendMessage(DM_SETFOCUS, ed_last_button, nullptr);
						return TRUE;
					}
					break;

				case KEY_RIGHT:
				case KEY_NUMPAD6:
				case KEY_TAB:
					if (Param1 == ed_last_button)
					{
						Dlg->SendMessage(DM_SETFOCUS, ed_first_button, nullptr);
						return TRUE;
					}
					break;

				case KEY_CTRLC:
				case KEY_RCTRLC:
				case KEY_CTRLINS:
				case KEY_RCTRLINS:
				case KEY_CTRLNUMPAD0:
				case KEY_RCTRLNUMPAD0:
					copy_information(Dlg);
					break;
				}
			}
		}
		break;

		case DN_CLOSE:
		{
			const auto& Data = *reinterpret_cast<dialog_data_type*>(Dlg->SendMessage(DM_GETDLGDATA, 0, nullptr));

			switch (Param1)
			{
			case ed_button_copy:
				copy_information(Dlg);
				return FALSE;

			case ed_button_stack:
				ShowStackTrace(GetStackTrace(Data.Module, tracer::get(Data.Module, *Data.Context->pointers(), Data.Context->thread_handle()), Data.NestedStack));
				return FALSE;

			case ed_button_minidump:
				{
					// TODO: subdirectory && timestamp
					auto Path = path::join(Global->Opt->LocalProfilePath, L"Far.mdmp"sv);

					if (write_minidump(*Data.Context, Path))
					{
						Message(0,
							msg(lng::MExcMinidump),
							{
								msg(lng::MExcMinidumpSuccess),
								std::move(Path)
							},
							{ lng::MOk });
					}
					else
					{
						const auto ErrorState = error_state::fetch();
						Message(MSG_WARNING, ErrorState,
							msg(lng::MError),
							{
								msg(lng::MEditCannotSave),
								std::move(Path)
							},
							{ lng::MOk });
					}

				}
				return FALSE;
			}
		}
		break;

	default:
		break;
	}
	return Dlg->DefProc(Msg,Param1,Param2);
}

static size_t max_item_size(span<string_view const> const Items)
{
	return std::max_element(ALL_CONST_RANGE(Items), [](string_view const Str1, string_view const Str2)
	{
		return Str1.size() < Str2.size();
	})->size();
}

static bool ExcDialog(
	span<string_view const> const Labels,
	span<string_view const> const Values,
	detail::exception_context const& Context,
	string_view const ModuleName,
	Plugin const* const PluginModule,
	std::vector<DWORD64> const* const NestedStack
)
{
	// TODO: Far Dialog is not the best choice for exception reporting
	// replace with something trivial

	const auto SysArea = 5;
	const auto C1X = 5;
	const auto C1W = static_cast<int>(max_item_size(Labels));
	const auto C2X = C1X + C1W + 1;
	const auto DlgW = std::max(80, std::min(ScrX + 1, static_cast<int>(C2X + max_item_size(Values) + SysArea + 1)));
	const auto C2W = DlgW - C2X - SysArea - 1;

	const auto DY = static_cast<int>(Values.size() + 8);

	auto EditDlg = MakeDialogItems<ed_items_count>(
	{
		{ DI_DOUBLEBOX, {{3,   1 }, {DlgW-4,DY-2}}, DIF_NONE, msg(lng::MExcTrappedException), },

#define LABEL_AND_VALUE(n)\
		{ DI_TEXT,  {{C1X, n+2 }, {C1X+C1W, n+2 }}, DIF_NONE, Labels[n], },\
		{ DI_EDIT,  {{C2X, n+2 }, {C2X+C2W, n+2 }}, DIF_READONLY | DIF_SELECTONENTRY, Values[n], }

		LABEL_AND_VALUE(0),
		LABEL_AND_VALUE(1),
		LABEL_AND_VALUE(2),
		LABEL_AND_VALUE(3),
		LABEL_AND_VALUE(4),
		LABEL_AND_VALUE(5),
		LABEL_AND_VALUE(6),
		LABEL_AND_VALUE(7),
		LABEL_AND_VALUE(8),
		LABEL_AND_VALUE(9),
		LABEL_AND_VALUE(10),
		LABEL_AND_VALUE(11),
		LABEL_AND_VALUE(12),

#undef LABEL_AND_VALUE

		{ DI_TEXT,      {{-1,DY-6}, {0,     DY-6}}, DIF_SEPARATOR, },
		{ DI_BUTTON,    {{0, DY-5}, {0,     DY-5}}, DIF_CENTERGROUP | DIF_FOCUS, msg(lng::MExcCopy), },
		{ DI_BUTTON,    {{0, DY-5}, {0,     DY-5}}, DIF_CENTERGROUP, msg(lng::MExcStack), },
		{ DI_BUTTON,    {{0, DY-5}, {0,     DY-5}}, DIF_CENTERGROUP, msg(lng::MExcMinidump), },
		{ DI_TEXT,      {{-1,DY-4}, {0,     DY-4}}, DIF_SEPARATOR, },
		{ DI_BUTTON,    {{0, DY-3}, {0,     DY-3}}, DIF_CENTERGROUP | DIF_DEFAULTBUTTON, msg(lng::MExcTerminate), },
		{ DI_BUTTON,    {{0, DY-3}, {0,     DY-3}}, DIF_CENTERGROUP | (PluginModule? 0 : DIF_DISABLE), msg(lng::MExcUnload), },
		{ DI_BUTTON,    {{0, DY-3}, {0,     DY-3}}, DIF_CENTERGROUP, msg(lng::MIgnore), },
	});

	dialog_data_type DlgData{ &Context, NestedStack, ModuleName };
	const auto Dlg = Dialog::create(EditDlg, ExcDlgProc, &DlgData);
	Dlg->SetDialogMode(DMODE_WARNINGSTYLE|DMODE_NOPLUGINS);
	Dlg->SetFlags(FSCROBJ_SPECIAL);
	Dlg->SetPosition({ -1, -1, DlgW, static_cast<int>(DY) });
	Dlg->Process();

	switch (Dlg->GetExitCode())
	{
	case ed_button_terminate:
		detail::UseTerminateHandler = true;
		[[fallthrough]];
	case ed_button_unload:
		return true;

	default:
		return false;
	}
}

static bool ExcConsole(
	span<string_view const> const Labels,
	span<string_view const> const Values,
	detail::exception_context const& Context,
	string_view const ModuleName,
	Plugin const* const PluginModule,
	std::vector<DWORD64> const* const NestedStack
)
{
	std::wcerr << L'\n';

	for (const auto& [m, v]: zip(Labels, Values))
	{
		const auto Label = fit_to_left(string(m), max_item_size(Labels));
		std::wcerr << Label << L' ' << v << L'\n';
	}

	ShowStackTrace(GetStackTrace(ModuleName, tracer::get(ModuleName, *Context.pointers(), Context.thread_handle()), NestedStack));
	std::wcerr << std::endl;

	if (!ConsoleYesNo(L"Terminate the process"sv, true))
		return false;

	detail::UseTerminateHandler = true;
	return true;
}

static bool ShowExceptionUI(
	bool const UseDialog,
	detail::exception_context const& Context,
	string_view const Exception,
	string_view const Details,
	error_state const& ErrorState,
	string_view const Function,
	string_view const Location,
	string_view const ModuleName,
	string_view const PluginInformation,
	Plugin const* const PluginModule,
	std::vector<DWORD64> const* const NestedStack
)
{
	SCOPED_ACTION(tracer::with_symbols)(PluginModule? ModuleName : L""sv);

	string Address, Name, Source;
	tracer::get_symbol(ModuleName, Context.pointers()->ExceptionRecord->ExceptionAddress, Address, Name, Source);

	if (!Name.empty())
		append(Address, L" - "sv, Name);

	if (Source.empty())
		Source = Location;

	const auto Errors = ErrorState.format_errors();
	const auto Version = self_version();
	const auto OsVersion = os_version();
	const auto KernelVersion = kernel_version();

	std::array Labels
	{
		L"Exception:"sv,
		L"Details:  "sv,
		L"errno:    "sv,
		L"LastError:"sv,
		L"NTSTATUS: "sv,
		L"Address:  "sv,
		L"Function: "sv,
		L"Source:   "sv,
		L"File:     "sv,
		L"Plugin:   "sv,
		L"Far:      "sv,
		L"OS:       "sv,
		L"Kernel:   "sv,
	};

	if (far_language::instance().is_loaded())
	{
		Labels =
		{
			msg(lng::MExcException),
			msg(lng::MExcDetails),
			L"errno:"sv,
			L"LastError:"sv,
			L"NTSTATUS:"sv,
			msg(lng::MExcAddress),
			msg(lng::MExcFunction),
			msg(lng::MExcSource),
			msg(lng::MExcFileName),
			msg(lng::MExcPlugin),
			msg(lng::MExcFarVersion),
			msg(lng::MExcOSVersion),
			msg(lng::MExcKernelVersion),
		};
	}

	const string_view Values[]
	{
		Exception,
		Details,
		Errors[0],
		Errors[1],
		Errors[2],
		Address,
		Function,
		Source,
		ModuleName,
		PluginInformation,
		Version,
		OsVersion,
		KernelVersion,
	};

	static_assert(std::size(Labels) == std::size(Values));
	static_assert(std::size(Labels) == (ed_last_line - ed_first_line) / 2 + 1);

	return (UseDialog? ExcDialog : ExcConsole)(Labels, span(Values), Context, ModuleName, PluginModule, NestedStack); // This goofy explicit span() is a workaround for GCC
}


template<size_t... I>
static constexpr uint32_t any_cc(std::string_view const Str, std::index_sequence<I...> Sequence)
{
	if (Str.size() != Sequence.size())
		throw;

	return (... | (Str[I] << 8 * I));
}

static constexpr uint32_t fourcc(std::string_view const Str)
{
	return any_cc(Str, std::make_index_sequence<4>{});
}

enum FARRECORDTYPE
{
	RTYPE_PLUGIN = fourcc("CPLG"sv), // информация о текущем плагине
};

namespace detail
{
	struct PMD
	{
		int mdisp; // Offset of intended data within base
		int pdisp; // Displacement to virtual base pointer
		int vdisp; // Index within vbTable to offset of base
	};

	using PMFN = int; // Image relative offset of Member Function

	struct CatchableType
	{
		unsigned int properties;        // Catchable Type properties (Bit field)
		int          pType;             // Image relative offset of TypeDescriptor
		PMD          thisDisplacement;  // Pointer to instance of catch type within thrown object.
		int          sizeOrOffset;      // Size of simple-type object or offset into buffer of 'this' pointer for catch object
		PMFN         copyFunction;      // Copy constructor or CC-closure
	};

	struct CatchableTypeArray
	{
		int nCatchableTypes;
		int arrayOfCatchableTypes; // Image relative offset of Catchable Types
	};

	struct ThrowInfo
	{
		unsigned int attributes;           // Throw Info attributes (Bit field)
		PMFN         pmfnUnwind;           // Destructor to call when exception has been handled or aborted
		int          pForwardCompat;       // Image relative offset of Forward compatibility frame handler
		int          pCatchableTypeArray;  // Image relative offset of CatchableTypeArray
	};
}

static bool IsCppException(const EXCEPTION_RECORD& Record)
{
	return Record.ExceptionCode == static_cast<DWORD>(EXCEPTION_MICROSOFT_CPLUSPLUS);
}

class [[nodiscard]] enum_catchable_objects: public enumerator<enum_catchable_objects, char const*>
{
	IMPLEMENTS_ENUMERATOR(enum_catchable_objects);

public:
	explicit enum_catchable_objects(EXCEPTION_RECORD const& Record)
	{
		if (!IsCppException(Record) || !Record.NumberParameters)
			return;

		m_BaseAddress = Record.NumberParameters == 4? Record.ExceptionInformation[3] : 0;
		const auto& ThrowInfoRef = *reinterpret_cast<detail::ThrowInfo const*>(Record.ExceptionInformation[2]);
		const auto& CatchableTypeArrayRef = *reinterpret_cast<detail::CatchableTypeArray const*>(m_BaseAddress + ThrowInfoRef.pCatchableTypeArray);
		m_CatchableTypesRVAs = { &CatchableTypeArrayRef.arrayOfCatchableTypes, static_cast<size_t>(CatchableTypeArrayRef.nCatchableTypes) };
	}

private:
	[[nodiscard, maybe_unused]]
	bool get(bool const Reset, char const*& Name) const
	{
		if (Reset)
			m_Index = 0;

		if (m_Index == m_CatchableTypesRVAs.size())
			return false;

		const auto& CatchableTypeRef = *reinterpret_cast<detail::CatchableType const*>(m_BaseAddress + m_CatchableTypesRVAs[m_Index++]);
		const auto& TypeInfoRef = *reinterpret_cast<std::type_info const*>(m_BaseAddress + CatchableTypeRef.pType);

		Name = TypeInfoRef.name();
		return true;
	}

	span<int const> m_CatchableTypesRVAs;
	size_t mutable m_Index{};
	uintptr_t m_BaseAddress{};
};

static string ExtractObjectType(EXCEPTION_RECORD const& xr)
{
	enum_catchable_objects const CatchableTypesEnumerator(xr);
	const auto Iterator = CatchableTypesEnumerator.cbegin();
	if (Iterator == CatchableTypesEnumerator.cend())
		return {};

	return encoding::utf8::get_chars(*Iterator);
}

static bool ProcessExternally(EXCEPTION_POINTERS* Pointers, Plugin const* const PluginModule)
{
	if (!Global || !Global->Opt->ExceptUsed || Global->Opt->strExceptEventSvc.empty())
		return false;

	const os::rtdl::module Module(Global->Opt->strExceptEventSvc);
	if (!Module)
		return false;

	struct PLUGINRECORD       // информация о плагине
	{
		DWORD TypeRec;          // Тип записи = RTYPE_PLUGIN
		DWORD SizeRec;          // Размер
		DWORD Reserved1[4];
		// DWORD SysID; GUID
		const wchar_t *ModuleName;
		DWORD Reserved2[2];    // резерв :-)
		DWORD SizeModuleName;
	};

	os::rtdl::function_pointer<BOOL(WINAPI*)(EXCEPTION_POINTERS*, const PLUGINRECORD*, const PluginStartupInfo*, DWORD*)> Function(Module, "ExceptionProc");
	if (!Function)
		return false;

	static PluginStartupInfo LocalStartupInfo;
	LocalStartupInfo = {};
	static FarStandardFunctions LocalStandardFunctions;
	LocalStandardFunctions = {};
	CreatePluginStartupInfo(&LocalStartupInfo, &LocalStandardFunctions);
	LocalStartupInfo.ModuleName = Global->Opt->strExceptEventSvc.c_str();
	static PLUGINRECORD PlugRec;

	if (PluginModule)
	{
		PlugRec = {};
		PlugRec.TypeRec = RTYPE_PLUGIN;
		PlugRec.SizeRec = sizeof(PlugRec);
		PlugRec.ModuleName = PluginModule->ModuleName().c_str();
	}

	DWORD dummy;
	return Function(Pointers, PluginModule ? &PlugRec : nullptr, &LocalStartupInfo, &dummy) != FALSE;
}

static bool handle_generic_exception(
	detail::exception_context const& Context,
	std::string_view const Function,
	string_view const Location,
	Plugin const* const PluginModule,
	string_view const Type,
	string_view const Message,
	error_state const& ErrorState = error_state::fetch(),
	std::vector<DWORD64> const* const NestedStack = nullptr
)
{
	static bool ExceptionHandlingIgnored = false;
	if (ExceptionHandlingIgnored)
		return false;


	if (Global)
		Global->ProcessException = true;

	if (ProcessExternally(Context.pointers(), PluginModule))
	{
		if (!PluginModule && Global)
			Global->CriticalInternalError = true;

		return true;
	}

	static const std::pair<string_view, NTSTATUS> KnownExceptions[]
	{
#define TEXTANDCODE(x) L###x##sv, x
		{TEXTANDCODE(EXCEPTION_ACCESS_VIOLATION)},
		{TEXTANDCODE(EXCEPTION_DATATYPE_MISALIGNMENT)},
		{TEXTANDCODE(EXCEPTION_BREAKPOINT)},
		{TEXTANDCODE(EXCEPTION_SINGLE_STEP)},
		{TEXTANDCODE(EXCEPTION_ARRAY_BOUNDS_EXCEEDED)},
		{TEXTANDCODE(EXCEPTION_FLT_DENORMAL_OPERAND)},
		{TEXTANDCODE(EXCEPTION_FLT_DIVIDE_BY_ZERO)},
		{TEXTANDCODE(EXCEPTION_FLT_INEXACT_RESULT)},
		{TEXTANDCODE(EXCEPTION_FLT_INVALID_OPERATION)},
		{TEXTANDCODE(EXCEPTION_FLT_OVERFLOW)},
		{TEXTANDCODE(EXCEPTION_FLT_STACK_CHECK)},
		{TEXTANDCODE(EXCEPTION_FLT_UNDERFLOW)},
		{TEXTANDCODE(EXCEPTION_INT_DIVIDE_BY_ZERO)},
		{TEXTANDCODE(EXCEPTION_INT_OVERFLOW)},
		{TEXTANDCODE(EXCEPTION_PRIV_INSTRUCTION)},
		{TEXTANDCODE(EXCEPTION_IN_PAGE_ERROR)},
		{TEXTANDCODE(EXCEPTION_ILLEGAL_INSTRUCTION)},
		{TEXTANDCODE(EXCEPTION_NONCONTINUABLE_EXCEPTION)},
		{TEXTANDCODE(EXCEPTION_STACK_OVERFLOW)},
		{TEXTANDCODE(EXCEPTION_INVALID_DISPOSITION)},
		{TEXTANDCODE(EXCEPTION_GUARD_PAGE)},
		{TEXTANDCODE(EXCEPTION_INVALID_HANDLE)},
		{TEXTANDCODE(EXCEPTION_POSSIBLE_DEADLOCK)},
		{TEXTANDCODE(CONTROL_C_EXIT)},
#undef TEXTANDCODE

		{L"C++ exception"sv, EXCEPTION_MICROSOFT_CPLUSPLUS},
	};

	string strFileName;

	const auto xr = Context.pointers()->ExceptionRecord;

	if (!PluginModule)
	{
		if (Global)
		{
			strFileName=Global->g_strFarModuleName;
		}
		else
		{
			// BUGBUG check result
			(void)os::fs::GetModuleFileName(nullptr, nullptr, strFileName);
		}
	}
	else
	{
		strFileName = PluginModule->ModuleName();
	}

	const auto AppendType = [](string& Str, string_view const ExceptionType)
	{
		append(Str, L" ("sv, ExceptionType, ')');
	};

	const auto WithType = [&](string&& Str)
	{
		if (!Type.empty())
		{
			AppendType(Str, Type);
			return std::move(Str);
		}

		if (const auto TypeStr = ExtractObjectType(*xr); !TypeStr.empty())
		{
			AppendType(Str, TypeStr);
		}
		return std::move(Str);
	};

	const auto Exception = [&](NTSTATUS Code)
	{
		const auto ItemIterator = std::find_if(CONST_RANGE(KnownExceptions, i) { return i.second == Code; });
		const auto Name = ItemIterator != std::cend(KnownExceptions)? ItemIterator->first : L"Unknown exception"sv;
		return WithType(format(FSTR(L"0x{0:0>8X} - {1}"), static_cast<DWORD>(Code), Name));
	}(Context.code());

	string Details;

	switch(static_cast<NTSTATUS>(Context.code()))
	{
	case EXCEPTION_ACCESS_VIOLATION:
	case EXCEPTION_IN_PAGE_ERROR:
		{
			const auto AccessedAddress = to_hex_wstring(xr->ExceptionInformation[1]);
			const auto Mode = [](ULONG_PTR Code)
			{
				switch (Code)
				{
				default:
				case 0: return L"read"sv;
				case 1: return L"written"sv;
				case 8: return L"executed"sv;
				}
			}(xr->ExceptionInformation[0]);

			Details = format(FSTR(L"Memory at {0} could not be {1}"), AccessedAddress, Mode);
		}
		break;

	case EXCEPTION_MICROSOFT_CPLUSPLUS:
		Details = Message;
		break;

	default:
		Details = os::GetErrorString(true, Context.code());
		break;
	}

	const auto PluginInformation = PluginModule? format(FSTR(L"{0} {1} ({2}, {3})"),
		PluginModule->Title(),
		version_to_string(PluginModule->version()),
		PluginModule->Description(),
		PluginModule->Author()
	) : L""s;

	if (!ShowExceptionUI(
		Global && Global->WindowManager && !Global->WindowManager->ManagerIsDown(),
		Context,
		Exception,
		Details,
		ErrorState,
		encoding::utf8::get_chars(Function),
		Location,
		strFileName,
		PluginInformation,
		PluginModule,
		NestedStack
	))
	{
		ExceptionHandlingIgnored = true;
		return false;
	}

	if (!PluginModule && Global)
		Global->CriticalInternalError = true;

	return true;
}

void restore_gpfault_ui()
{
	disable_exception_handling();
	os::unset_error_mode(SEM_NOGPFAULTERRORBOX);
}

static std::pair<string, string> extract_nested_exceptions(EXCEPTION_RECORD const& Record, const std::exception& Exception, bool Top = true)
{
	std::pair<string, string> Result;
	auto& [ObjectType, What] = Result;

	ObjectType = ExtractObjectType(Record);

	if (ObjectType.empty())
		ObjectType = L"std::exception"sv;

	// far_exception.what() returns additional information (function, file and line).
	// We don't need it on top level because it's extracted separately
	if (const auto FarException = Top? dynamic_cast<const detail::far_base_exception*>(&Exception) : nullptr)
		What = FarException->message();
	else
		What = encoding::utf8::get_chars(Exception.what());

	try
	{
		std::rethrow_if_nested(Exception);
	}
	catch (const std::exception& e)
	{
		const auto& [NestedObjectType, NestedWhat] = extract_nested_exceptions(*tracer::get_pointers().ExceptionRecord, e, false);
		ObjectType = concat(NestedObjectType, L" -> "sv, ObjectType);
		What = concat(NestedWhat, L" -> "sv, What);
	}
	catch (...)
	{
		auto NestedObjectType = ExtractObjectType(*tracer::get_pointers().ExceptionRecord);
		if (NestedObjectType.empty())
			NestedObjectType = L"Unknown"sv;

		ObjectType = concat(NestedObjectType, L" -> "sv, ObjectType);
		What = concat(L"?"sv, L" -> "sv, What);
	}

	return Result;
}

static bool handle_std_exception(
	detail::exception_context const& Context,
	const std::exception& e,
	std::string_view const Function,
	const Plugin* const Module
)
{
	if (const auto SehException = dynamic_cast<const seh_exception*>(&e))
	{
		auto NestedStack = tracer::get({}, *SehException->context().pointers(), SehException->context().thread_handle());
		return handle_generic_exception(Context, Function, {}, Module, {}, {}, *SehException, &NestedStack);
	}

	const auto& [Type, What] = extract_nested_exceptions(*Context.pointers()->ExceptionRecord, e);

	if (const auto FarException = dynamic_cast<const detail::far_base_exception*>(&e))
	{
		const auto NestedStack = [&]
		{
			const auto Wrapper = dynamic_cast<const far_wrapper_exception*>(&e);
			return Wrapper ? &Wrapper->get_stack() : nullptr;
		}();

		return handle_generic_exception(Context, FarException->function(), FarException->location(), Module, Type, What, *FarException, NestedStack);
	}

	return handle_generic_exception(Context, Function, {}, Module, Type, What);
}

bool handle_std_exception(const std::exception& e, std::string_view const Function, const Plugin* const Module)
{
	detail::exception_context const Context(EXCEPTION_MICROSOFT_CPLUSPLUS, tracer::get_pointers(), os::OpenCurrentThread(), GetCurrentThreadId());

	return handle_std_exception(Context, e, Function, Module);
}

static bool handle_seh_exception(
	detail::exception_context const& Context,
	std::string_view const Function,
	Plugin const* const PluginModule
)
{
	enum_catchable_objects const CatchableTypesEnumerator(*Context.pointers()->ExceptionRecord);
	std::vector<char const*> const CatchableTypes(ALL_CONST_RANGE(CatchableTypesEnumerator));
	if (std::find_if(ALL_CONST_RANGE(CatchableTypes), [](char const* Name) { return strstr(Name, "std::exception") != nullptr; }) != CatchableTypes.cend())
	{
		return handle_std_exception(Context, *reinterpret_cast<std::exception const*>(Context.pointers()->ExceptionRecord->ExceptionInformation[1]), Function, PluginModule);
	}

	return handle_generic_exception(Context, Function, {}, PluginModule, {}, {});
}

bool handle_unknown_exception(std::string_view const Function, const Plugin* const Module)
{
	// C++ exception to EXCEPTION_POINTERS translation relies on Microsoft implementation.
	// It won't work in gcc etc.
	// Set ExceptionCode manually so ProcessGenericException will at least report it as C++ exception
	const detail::exception_context Context(EXCEPTION_MICROSOFT_CPLUSPLUS, tracer::get_pointers(), os::OpenCurrentThread(), GetCurrentThreadId());
	return handle_seh_exception(Context, Function, Module);
}

bool use_terminate_handler()
{
	return detail::UseTerminateHandler;
}

static LONG WINAPI unhandled_exception_filter_impl(EXCEPTION_POINTERS* const Pointers)
{
	if (!detail::HandleSehExceptions)
		return EXCEPTION_CONTINUE_SEARCH;

	detail::set_fp_exceptions(false);
	const detail::exception_context Context(Pointers->ExceptionRecord->ExceptionCode, *Pointers, os::OpenCurrentThread(), GetCurrentThreadId());
	if (handle_seh_exception(Context, __FUNCTION__, {}))
	{
		std::_Exit(EXIT_FAILURE);
	}
	restore_gpfault_ui();
	return EXCEPTION_CONTINUE_SEARCH;
}

unhandled_exception_filter::unhandled_exception_filter():
	m_PreviousFilter(SetUnhandledExceptionFilter(unhandled_exception_filter_impl))
{
}

unhandled_exception_filter::~unhandled_exception_filter()
{
	SetUnhandledExceptionFilter(m_PreviousFilter);
}

namespace detail
{
	void cpp_try(function_ref<void()> const Callable, function_ref<void()> const UnknownHandler, function_ref<void(std::exception const&)> const StdHandler)
	{
		if (!HandleCppExceptions)
		{
			return Callable();
		}

		if (StdHandler)
		{
			try
			{
				return Callable();
			}
			catch (std::exception const& e)
			{
				return StdHandler(e);
			}
			catch (...)
			{
				return UnknownHandler();
			}
		}

		try
		{
			return Callable();
		}
		catch (...)
		{
			return UnknownHandler();
		}
	}

	static thread_local bool StackOverflowHappened;

	void seh_try(function_ref<void()> const Callable, function_ref<DWORD(DWORD, EXCEPTION_POINTERS*)> const Filter, function_ref<void()> const Handler)
	{
#if COMPILER(GCC) || (COMPILER(CLANG) && !defined _WIN64 && defined __GNUC__)
		// GCC doesn't support these currently
		// Clang x86 crashes with "Assertion failed: STI.isTargetWindowsMSVC() && "funclets only supported in MSVC env""
		return Callable();
#else
#if COMPILER(CLANG)
		// Workaround for clang "filter expression type should be an integral value" error
		const auto FilterWrapper = [&](DWORD const Code, EXCEPTION_POINTERS* const Pointers){ return Filter(Code, Pointers); };
#define Filter FilterWrapper
#endif

		__try
		{
			Callable();
		}
		__except (set_fp_exceptions(false), Filter(GetExceptionCode(), GetExceptionInformation()))
		{
			if (StackOverflowHappened)
			{
				if (!_resetstkoflw())
					std::_Exit(EXIT_FAILURE);

				StackOverflowHappened = false;
			}

			Handler();
		}

#if COMPILER(CLANG)
#undef Filter
#endif
#endif
	}

	int seh_filter(int const Code, const EXCEPTION_POINTERS* const Info, std::string_view const Function, const Plugin* const Module)
	{
		if (HandleSehExceptions)
		{
			const exception_context Context(Code, *Info, os::OpenCurrentThread(), GetCurrentThreadId());
			if (Code == EXCEPTION_STACK_OVERFLOW)
			{
				bool Result = false;
				{
					os::thread(&os::thread::join, [&]{ Result = handle_seh_exception(Context, Function, Module); });
				}

				StackOverflowHappened = true;

				if (Result)
				{
					return EXCEPTION_EXECUTE_HANDLER;
				}
			}
			else
			{
				if (handle_seh_exception(Context, Function, Module))
					return EXCEPTION_EXECUTE_HANDLER;
			}
		}

		restore_gpfault_ui();
		return EXCEPTION_CONTINUE_SEARCH;
	}

	int seh_thread_filter(std::exception_ptr& Ptr, DWORD const Code, EXCEPTION_POINTERS* const Info)
	{
		// SEH transport between threads is currenly implemented in terms of C++ exceptions, so it requires both
		if (!(HandleSehExceptions && HandleCppExceptions))
		{
			restore_gpfault_ui();
			return EXCEPTION_CONTINUE_SEARCH;
		}

		Ptr = std::make_exception_ptr(seh_exception(Code, *Info, os::OpenCurrentThread(), GetCurrentThreadId()));
		return EXCEPTION_EXECUTE_HANDLER;
	}

	void seh_thread_handler()
	{
		// The thread is about to quit, but we still need it to get a stack trace.
		// It will be released once the corresponding exception context is destroyed.
		// The caller MUST detach it if ExceptionPtr is not empty.
		SuspendThread(GetCurrentThread());
	}

	void set_fp_exceptions(bool const Enable)
	{
		_clearfp();
		_controlfp(Enable? 0 : _MCW_EM, _MCW_EM);
	}
}

#ifdef ENABLE_TESTS

#include "testing.hpp"

TEST_CASE("fourcc")
{
	static_assert(fourcc("CPLG"sv) == 0x474C5043);
	static_assert(fourcc("avc1"sv) == 0x31637661);

	SUCCEED();
}
#endif
