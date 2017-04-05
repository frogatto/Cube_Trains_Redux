#include <stdio.h>

#ifndef NO_EDITOR
#include "editor.hpp"
#endif

#include "asserts.hpp"
#include "level.hpp"
#include "stats.hpp"
#include "variant.hpp"

#if defined(_WINDOWS)
#include "SDL_syswm.h"
#endif

void report_assert_msg(const std::string& m)
{
	if(level::current_ptr()) {
		std::cerr << "ATTEMPTING TO SEND CRASH REPORT...\n";
		std::map<variant,variant> obj;
		obj[variant("type")] = variant("crash");
		obj[variant("msg")] = variant(m);
#ifndef NO_EDITOR
		obj[variant("editor")] = variant(editor::last_edited_level().empty() == false);
#else
		obj[variant("editor")] = variant(false);
#endif
		stats::record(variant(&obj), level::current_ptr()->id());
		stats::flush_and_quit();
	}

#if defined(__native_client__)
	std::cerr << m;
#endif

#if defined(__ANDROID__)
	__android_log_print(ANDROID_LOG_INFO, "Frogatto", m.c_str());

#endif
	
#if defined(_WINDOWS)
	SDL_SysWMinfo SysInfo;
	SDL_VERSION(&SysInfo.version);
	if(SDL_GetWMInfo(&SysInfo) > 0) {
		::MessageBoxA(SysInfo.window, m.c_str(), "Assertion failed", MB_OK|MB_ICONSTOP);
	}
#endif
}

validation_failure_exception::validation_failure_exception(const std::string& m)
  : msg(m)
{
	std::cerr << "ASSERT FAIL: " << m << "\n";
}

namespace {
	int throw_validation_failure = 0;
}

bool throw_validation_failure_on_assert()
{
	return throw_validation_failure != 0;
}

assert_recover_scope::assert_recover_scope()
{
	throw_validation_failure++;
}

assert_recover_scope::~assert_recover_scope()
{
	throw_validation_failure--;
}

void output_backtrace()
{
	std::cerr << get_call_stack() << "\n";
}
