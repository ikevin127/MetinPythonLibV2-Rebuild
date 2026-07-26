#pragma once
#include "stdafx.h"
#include "App.h"
#include "../common/utils.h"
#include <io.h>

HANDLE threadID;

//std::map<DWORD,DWORD> instances;

//Supposed to be a std::vector
struct DLLArgs {
	int size;
	int reserved;
	char path[256];
};

/*
void SetupDebugFile()
{
	std::string log_path(getDllPath());
	log_path += "ex_log.txt";

	HANDLE new_stdout = CreateFileA(log_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	SetStdHandle(STD_OUTPUT_HANDLE, new_stdout);
	int fd = _open_osfhandle((intptr_t)new_stdout, O_WRONLY | O_TEXT);
	_dup2(fd, 1);
}
void SetupConsole()
{
	AllocConsole();
	freopen("CONOUT$", "wb", stdout);
	freopen("CONOUT$", "wb", stderr);
	freopen("CONIN$", "rb", stdin);
	SetConsoleTitle("Debug Console");

}*/


// ============================ CRASH LOGGER (vectored exception handler) ============================
// The client has anti-debug that crashes when a breakpoint is set, and the world-reload crash is a
// hard C-level access violation that leaves no python traceback in syserr. A VECTORED exception
// handler is NOT a debugger (no INT3, no debug registers, no attached process) -- Windows just calls
// it in-process, first-chance, at the faulting instruction, before the client's own crash handler.
// We dump the fault context (instruction, registers, candidate PyObjects, stack return addresses) to
// <clientroot>\exlib_crash.txt, then CONTINUE_SEARCH so the crash proceeds exactly as before.
// Overwrites each access violation -> the file holds the LAST (fatal) one when the process dies.
extern HMODULE hDll;   // eXLib.mix base (set in DllMain)

// SEH-guarded read -- MUST stay in its own function (no C++ object unwinding alongside __try).
static bool exSafeRead(const void* addr, void* buf, size_t n)
{
	__try { memcpy(buf, addr, n); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Interpret a register value as a possible PyObject* and dump ob_refcnt / ob_type / tp_name.
static void exDumpObj(FILE* f, const char* name, DWORD ptr)
{
	DWORD w[8];
	if (ptr < 0x10000 || !exSafeRead((const void*)ptr, w, sizeof(w))) {
		fprintf(f, "  %-4s=%08X  (unreadable/low)\n", name, ptr);
		return;
	}
	// PyObject: +0 ob_refcnt, +4 ob_type. A freed+reused slot shows a garbage/ASCII ob_type.
	fprintf(f, "  %-4s=%08X  refcnt=%d ob_type=%08X  raw:", name, ptr, (int)w[0], w[1]);
	for (int i = 0; i < 8; i++) fprintf(f, " %08X", w[i]);
	fprintf(f, "\n");
	DWORD tp = w[1];
	if (tp > 0x10000) {
		DWORD tnamePtr = 0;               // PyTypeObject.tp_name is at +0x0C (2.7 32-bit)
		if (exSafeRead((const void*)(tp + 0x0C), &tnamePtr, 4) && tnamePtr > 0x10000) {
			char nm[64] = { 0 };
			if (exSafeRead((const void*)tnamePtr, nm, 63)) {
				for (int i = 0; i < 63; i++) if (nm[i] && (nm[i] < 32 || nm[i] > 126)) { nm[i] = 0; break; }
				fprintf(f, "        %s->tp_name = '%s'\n", name, nm);
			}
		}
	}
}

static void exWriteCrashReport(EXCEPTION_POINTERS* ep)
{
	char path[300];
	_snprintf(path, sizeof(path), "%sexlib_crash.txt", getDllPath());
	FILE* f = fopen(path, "w");
	if (!f) return;
	setvbuf(f, NULL, _IONBF, 0);   // UNBUFFERED: each fprintf hits disk now, so a crash mid-report
	                               // still leaves everything written so far (the 0-byte file was a
	                               // buffered report lost when the client killed the process).

	EXCEPTION_RECORD* r = ep->ExceptionRecord;
	CONTEXT* c = ep->ContextRecord;
	DWORD exe = (DWORD)GetModuleHandleA(NULL);
	DWORD py = (DWORD)GetModuleHandleA("python27.dll");
	DWORD ex = (DWORD)hDll;

	fprintf(f, "=== eXLib crash report ===\n");
	fprintf(f, "code=0x%08X  flags=0x%08X  faultEIP=0x%08X\n",
		r->ExceptionCode, r->ExceptionFlags, (DWORD)r->ExceptionAddress);
	if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2)
		fprintf(f, "access=%s  badAddr=0x%08X\n",
			r->ExceptionInformation[0] ? "WRITE" : "READ", (DWORD)r->ExceptionInformation[1]);
	fprintf(f, "modbase: exe=%08X  python27=%08X  eXLib=%08X\n", exe, py, ex);
	fprintf(f, "EAX=%08X EBX=%08X ECX=%08X EDX=%08X\n", c->Eax, c->Ebx, c->Ecx, c->Edx);
	fprintf(f, "ESI=%08X EDI=%08X EBP=%08X ESP=%08X EIP=%08X\n", c->Esi, c->Edi, c->Ebp, c->Esp, c->Eip);

	fprintf(f, "code@EIP:");
	for (int i = 0; i < 16; i++) { BYTE b; if (exSafeRead((const BYTE*)c->Eip + i, &b, 1)) fprintf(f, " %02X", b); }
	fprintf(f, "\n");

	fprintf(f, "candidate objects:\n");
	exDumpObj(f, "EAX", c->Eax);
	exDumpObj(f, "EDI", c->Edi);
	exDumpObj(f, "ECX", c->Ecx);
	exDumpObj(f, "ESI", c->Esi);
	exDumpObj(f, "EDX", c->Edx);

	// The fault is inside PyObject_GetAttrString(o=[esp+8], name=[esp+0xC]) -- capture BOTH args.
	// 'name' is a static char* in the client binary; reading it at runtime reveals WHICH attribute the
	// client's C++ is fetching on the freed object -> identifies what the freed object was supposed to be.
	{
		DWORD oarg = 0, narg = 0;
		exSafeRead((const void*)(c->Esp + 8), &oarg, 4);
		exSafeRead((const void*)(c->Esp + 0xC), &narg, 4);
		char nm[96] = { 0 };
		bool ok = (narg > 0x10000) && exSafeRead((const void*)narg, nm, 95);
		if (ok) for (int i = 0; i < 95; i++) if (nm[i] && (nm[i] < 32 || nm[i] > 126)) { nm[i] = 0; break; }
		fprintf(f, "getattr: o=%08X  name@%08X='%s'\n", oarg, narg, ok ? nm : "(unreadable)");
	}

	// The exe is packed on disk (offline disasm = zeros), but the code is unpacked in memory now. Dump
	// bytes around the first few distinct exe return addresses so the client's real functions can be
	// disassembled offline (the call to PyObject_GetAttrString sits just before each return address).
	fprintf(f, "code around exe stack frames (runtime-unpacked, -0x20..+0x10):\n");
	DWORD seen[8]; int nseen = 0;
	for (int i = 0; i < 160 && nseen < 6; i++) {
		DWORD v; if (!exSafeRead((const BYTE*)(c->Esp + i * 4), &v, 4)) break;
		if (!(exe && v >= exe && v < exe + 0x5000000)) continue;
		bool dup = false; for (int j = 0; j < nseen; j++) if (seen[j] == v) dup = true;
		if (dup) continue;
		seen[nseen++] = v;
		fprintf(f, "  exe+%08X:", v - exe);
		for (int k = -0x20; k < 0x10; k++) { BYTE b; if (exSafeRead((const BYTE*)((INT_PTR)v + k), &b, 1)) fprintf(f, " %02X", b); else fprintf(f, " ??"); }
		fprintf(f, "\n");
	}

	fprintf(f, "stack return-addresses (ESP + N):\n");
	for (int i = 0; i < 160; i++) {
		DWORD v; if (!exSafeRead((const BYTE*)(c->Esp + i * 4), &v, 4)) break;
		if (exe && v >= exe && v < exe + 0x5000000) fprintf(f, "  +%03X: %08X  exe+%08X\n", i * 4, v, v - exe);
		else if (py && v >= py && v < py + 0x400000)  fprintf(f, "  +%03X: %08X  python27+%06X\n", i * 4, v, v - py);
		else if (ex && v >= ex && v < ex + 0x100000)  fprintf(f, "  +%03X: %08X  eXLib+%06X\n", i * 4, v, v - ex);
	}
	fclose(f);
}

static volatile LONG g_exInHandler = 0;
static volatile LONG g_exRecovered = 0;
static LONG WINAPI exVehHandler(EXCEPTION_POINTERS* ep)
{
	if (ep && ep->ExceptionRecord && ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
		CONTEXT* c = ep->ContextRecord;
		DWORD py = (DWORD)GetModuleHandleA("python27.dll");

		// ---- WORLD-RELOAD UAF RECOVERY ----------------------------------------------------------
		// On teleport uBot's object churn causes the client's game/phase-window python object to be
		// freed while the client's C++ still holds the pointer; the next frame the client calls
		// PyObject_GetAttrString(freed_window, "BINARY_...") and faults reading
		// o->ob_type->tp_getattr at python27+0xAB458 (badAddr = ASCII garbage). That function is
		// allowed to return NULL, and the client's getattr wrapper handles NULL (PyErr_Clear, skips
		// the call). So instead of dying, RETURN NULL: set EAX=0 and jump to the function's clean
		// 'pop edi; ret' epilogue at +0xAB46A. The frame is skipped; the next world frame installs a
		// fresh window. This instruction only faults when ob_type is already a bad pointer (a UAF),
		// so recovering here is strictly safer than crashing. (Mitigation -- the underlying uBot
		// over-decref is still being hunted; keep the crash log for the first few occurrences.)
		if (py && c->Eip == py + 0x0AB458) {
			extern bool ExToggle_RecoverGetattr();   // App.cpp -- recover_getattr toggle
			if (InterlockedIncrement(&g_exRecovered) <= 5)
				exWriteCrashReport(ep);          // capture for diagnostics (whether we recover or not)
			if (ExToggle_RecoverGetattr()) {
				c->Eax = 0;                      // PyObject_GetAttrString -> NULL (attribute miss)
				c->Eip = py + 0x0AB46A;          // -> 'pop edi; ret' epilogue (clean __cdecl return)
				return EXCEPTION_CONTINUE_EXECUTION; // resume the client; no crash
			}
			return EXCEPTION_CONTINUE_SEARCH;    // recovery DISABLED -> let it crash (verifies the pin alone)
		}
		// -----------------------------------------------------------------------------------------

		// any OTHER access violation: log once (unbuffered) and let it crash as before.
		if (InterlockedCompareExchange(&g_exInHandler, 1, 0) == 0) {
			exWriteCrashReport(ep);
			InterlockedExchange(&g_exInHandler, 0);
		}
	}
	return EXCEPTION_CONTINUE_SEARCH;   // not handled -- proceed as normal
}
// ==================================================================================================


DWORD WINAPI ThreadProc(LPVOID lpParameter)
{
	static CApp app = CApp();
	app.init();
	//MessageBox(NULL, "Success Loading", "SUCCESS", MB_OK);
	return true;
}


BOOLEAN WINAPI DllMain(IN HINSTANCE hDllHandle,
	IN DWORD     nReason,
	IN LPVOID    Reserved)
{


	//  Perform global initialization.
	char test[256] = { 0 };

	switch (nReason)
	{
	case DLL_PROCESS_ATTACH:
		if (Reserved) {
			DLLArgs* dl = (DLLArgs*)Reserved;
			setDllPath(dl->path);
		}
		else {
			GetModuleFileNameA(hDllHandle, test, 256);
			setDllPath(test);
		}
		hDll = (HMODULE)hDllHandle;
		AddVectoredExceptionHandler(1, exVehHandler);   // first-chance crash logger -> exlib_crash.txt
		threadID = CreateThread(NULL, 0, ThreadProc, NULL, 0, NULL);
		break;

	case DLL_PROCESS_DETACH:
		CApp & app = CApp::Instance();
		DEBUG_INFO_LEVEL_1("DETACHED CALLED");
		//app.exit();
		break;
	}

	return true;
}

