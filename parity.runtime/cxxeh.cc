/****************************************************************\
*                                                                *
* Copyright (C) 2007 by Markus Duft <markus.duft@salomon.at>     *
*                                                                *
* This file is part of parity.                                   *
*                                                                *
* parity is free software: you can redistribute it and/or modify *
* it under the terms of the GNU Lesser General Public License as *
* published by the Free Software Foundation, either version 3 of *
* the License, or (at your option) any later version.            *
*                                                                *
* parity is distributed in the hope that it will be useful,      *
* but WITHOUT ANY WARRANTY; without even the implied warranty of *
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the  *
* GNU Lesser General Public License for more details.            *
*                                                                *
* You should have received a copy of the GNU Lesser General      *
* Public License along with parity. If not,                      *
* see <http://www.gnu.org/licenses/>.                            *
*                                                                *
\****************************************************************/

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0500
#endif

#include <stdio.h>
#include <stdlib.h>
#include <process.h>
#include <windows.h>
#include "internal/output.h"
#include "internal/diagnose.h"

#include <typeinfo>
#include <exception>

//
// The following code is a bridge from SEH back to C++ EH.
// - many thanks to http://members.gamedev.net/sicrane/articles/exception.html
//

struct UntypedException {
  UntypedException(const EXCEPTION_RECORD & er)
    : exception_object(reinterpret_cast<void *>(er.ExceptionInformation[1])),
      type_array(reinterpret_cast<_ThrowInfo *>(er.ExceptionInformation[2])->pCatchableTypeArray)
  {}
  void * exception_object;
  _CatchableTypeArray * type_array;
};

void * exception_cast_worker(const UntypedException & e, const type_info & ti) {
  for (int i = 0; i < e.type_array->nCatchableTypes; ++i) {
    _CatchableType & type_i = *e.type_array->arrayOfCatchableTypes[i];
    const std::type_info & ti_i = *reinterpret_cast<std::type_info *>(type_i.pType);
    if (ti_i == ti) {
      char * base_address = reinterpret_cast<char *>(e.exception_object);
      base_address += type_i.thisDisplacement.mdisp;
      return base_address;
    }
  }
  return 0;
}

template <typename T>
T * exception_cast(const UntypedException & e) {
  const std::type_info & ti = typeid(T);
  return reinterpret_cast<T *>(exception_cast_worker(e, ti));
}

//
// ----------------------------------------------------------------------
//

//
// Handler for "unhandled" exceptions, thus every problem that propagates back
// up to PcrtCxxEhRunEntryGuarded.
//
static LONG CALLBACK PcrtUnhandledException(struct _EXCEPTION_POINTERS* ex) {
	// extract C++ type information.
	const EXCEPTION_RECORD& er = *ex->ExceptionRecord;

	// find out wether this is a C++ or SEH exception.
	switch (er.ExceptionCode) {
	case 0xE0000000|'msc': // code for C++ exceptions
		{
			// on C++ exception, try to get useful information by mapping RTTI from the ExceptionRecord.
			UntypedException ue(er);
			if(std::exception* e = exception_cast<std::exception>(ue)) {
				const std::type_info& ti = typeid(*e);
				PcrtOutPrint(GetStdHandle(STD_ERROR_HANDLE), "unhandled C++ exception of type %s: \"%s\"\n", ti.name(), e->what());
			} else {
				PcrtOutPrint(GetStdHandle(STD_ERROR_HANDLE), "unhandled C++ exception of unknown type!\n");
			}
		}
		break;
	default:
		// write detailed information about the exception - not C++
		PcrtWriteExceptionInformation(GetStdHandle(STD_ERROR_HANDLE), ex, 0);
		break;
	}

	// "old" style handling also: write core file.
	PcrtHandleException(ex);

	// always execute handler - causes terminate.
	return EXCEPTION_EXECUTE_HANDLER;
}

static void PcrtInvalidCRTParameter(const wchar_t* exp, const wchar_t* func, const wchar_t* file, unsigned int line, uintptr_t pRes) {
	// arguments only valid on DEBUG CRT! for now, ignore them.
	PcrtOutPrint(GetStdHandle(STD_ERROR_HANDLE), "Invalid parameter to C-Runtime function detected. Terminating.\n");
	PcrtBreakIfDebugged();
	terminate();
}

//
// Runs the real entry point given to the linker, guarding it with a __try
// __except to catch any exceptions that happen to fly by. This prevents the
// "normal" unhandled exception handler from running, which would not terminate
// the process if being debugged, causeing cool issues (like continuing execution
// right after the throw when pressing "Continue" in the debugger.
//
static int PcrtCxxEhRunEntryGuarded(int(*realEntry)()) {
	__try {
		if(GetEnvironmentVariableA("PCRT_ENABLE_CRASHBOXES", NULL, 0) == 0) {
			//
			// Disable various error boxes, which we no longer need,
			// as we create core files after seting up exception
			// handling.
			//
			SetErrorMode(SetErrorMode(0) | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
		}

		_set_invalid_parameter_handler(PcrtInvalidCRTParameter);

		return realEntry();
	} __except(PcrtUnhandledException(GetExceptionInformation())) {
		PcrtBreakIfDebugged();
		terminate();
	}

	return -1; // not reached
}

//
// Entry point wrapped around real entry. This in turn is already called by a
// (generated by the linker code) wrapper that called PcrtInit first already.
//
extern "C" int PcrtCxxEhStartup(int(*realEntry)()) {

	// from MS-CRT: security cookie /has/ to be initialized befor the first
	// function is called that does exception handling (PcrtCxxEhRunEntryGuarded).
	// mainCRTStartup will call this again before calling __tmainCRTStartup, but
	// that's ok, as __security_init_cookie can cope with that.
	__security_init_cookie();

	// now call entry guard, which will wrap in exception handlers.
	return PcrtCxxEhRunEntryGuarded(realEntry);
}
