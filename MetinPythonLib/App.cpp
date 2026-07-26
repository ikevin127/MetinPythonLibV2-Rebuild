#include "stdafx.h"
#include "App.h"
#include "PythonModule.h"
#include "Memory.h"
#include "InstanceManager.h"
#include "Player.h"
#include "Background.h"
#include "NetworkStream.h"
#include "Communication.h"
#include "../common/Config.h"
#include <set>

HMODULE hDll = 0;

// -------------------------------------------------------------------------------------------------
// MODULE PINNING -- fixes the world-reload use-after-free crash.
// Crash evidence (exlib_crash.txt): the client faults in python27 getattr on a FREED python object
// (refcnt=0) whose memory was reused by the module-path string "OpenBot.Modules.Actions" -- i.e. a
// module object was over-decref'd to zero and freed while the client still resolves that dotted
// import on a world reload. Modules are meant to be immortal (they live in sys.modules for the whole
// process), so a stray decref freeing one is the bug. We give every module ONE permanent extra ref:
// a stray over-decref then can't drop it to zero, so the dangling-module-pointer getattr can't happen.
// Idempotent (a std::set skips already-pinned modules), so it's cheap to call on every world re-entry
// to also cover modules imported lazily after first load. Must run under the GIL (callers hold it).
static std::set<PyObject*> g_pinnedModules;
static void pinAllModules()
{
	PyObject* modules = PyImport_GetModuleDict();   // borrowed reference to sys.modules
	if (!modules || !PyDict_Check(modules))
		return;
	PyObject *key, *value;
	Py_ssize_t pos = 0;
	while (PyDict_Next(modules, &pos, &key, &value)) {   // increfing values does not mutate the dict
		if (value && PyModule_Check(value) && g_pinnedModules.find(value) == g_pinnedModules.end()) {
			Py_INCREF(value);                            // immortalize -- a stray decref can't free it
			g_pinnedModules.insert(value);
		}
	}
}

// -------------------------------------------------------------------------------------------------
// GAME-WINDOW PIN -- the ROOT fix for the world-reload UAF (backstopped by exVehHandler in main.cpp).
// CPythonNetworkStream::SetPhaseWindow stores the game window BORROWED (no Py_INCREF); the client then
// calls methods on m_apoPhaseWnd[PHASE_WINDOW_GAME] every frame. uBot's object churn drops the window's
// last python reference while that borrowed pointer is still live -> use-after-free. We hold ONE ref to
// the current game window so it can never reach refcount 0 while active; when the client installs a new
// window we release the old pin (it frees normally) and pin the new one. Verified live via CE (26.1.11):
//   singleton = CMemory::getNetworkStream() (live __CheckPacket 'this'). The old hardcoded
//   exeBase+0x02EACCD4 global went STALE on GF 26.1.11 -> reads 0 -> pin no-oped -> keys died; see below.
//   m_apoPhaseWnd at singleton + 0x39C ; PHASE_WINDOW_GAME = 5 (slot's tp_name = "GameWindow")
//   -> game window = *(singleton + 0x39C + 5*4) = *(singleton + 0x3B0)
// Every read is range-checked + SEH-guarded, so a wrong offset on a future client just no-ops (and the
// VEH recovery still catches any residual crash). Runs under the GIL from the process loop.
static bool exReadDwordSafe(DWORD addr, DWORD* out)
{
	__try { *out = *(volatile DWORD*)addr; return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static PyObject* g_pinnedGameWnd = 0;
static void pinGameWindow()
{
	// Source the CPythonNetworkStream from eXLib's OWN live pointer -- the same 'this' the working packet
	// senders use: getNetworkStream() is captured by the __CheckPacket hook ("proven correct"), and once
	// in-game packets flow every frame so it's populated. The old hardcoded (GetModuleHandle(NULL)+0x02EACCD4)
	// global is STALE on GF 26.1.11 -- verified LIVE it reads 0 with the correct exe base (0x00320000), so
	// pinGameWindow silently no-oped, the game window was NEVER held alive, and the VEH recovery had to NULL
	// that freed window's getattrs every frame -> that ALSO killed I/V/C key handling (mouse buttons live in
	// separate windows, hence they survived). m_apoPhaseWnd is a CPythonNetworkStream member at +0x39C;
	// PHASE_WINDOW_GAME=5 -> game window = *(stream + 0x3B0) (offset verified live: a real GameWindow).
	DWORD singleton = (DWORD)CMemory::Instance().getNetworkStream();
	if (singleton < 0x00100000 || singleton >= 0x7F000000)
		return;                                          // stream not captured yet (no packet) -> safe no-op
	DWORD cur = 0;
	if (!exReadDwordSafe(singleton + 0x3B0, &cur))       // m_apoPhaseWnd[PHASE_WINDOW_GAME]
		return;
	if (cur == (DWORD)g_pinnedGameWnd)
		return;                                          // unchanged -> nothing to do
	if (cur == 0)
		return;                                          // slot cleared (transient during reload) -> KEEP the
	                                                     // current pin until a real new window is installed,
	                                                     // so cached copies of the old window can't dangle
	if (cur < 0x00100000 || cur >= 0x7F000000)           // validate it's a live PyObject before Py_INCREF
		return;
	DWORD refcnt = 0, obtype = 0;
	if (!exReadDwordSafe(cur, &refcnt) || refcnt == 0 || refcnt > 0x10000000)
		return;                                          // refcnt 0/absurd -> freed or not an object
	if (!exReadDwordSafe(cur + 4, &obtype) || obtype < 0x00100000 || obtype >= 0x7F000000)
		return;                                          // ob_type unreadable -> not a live object
	Py_XINCREF((PyObject*)cur);                          // pin the new game window (can't hit 0 while active)
	Py_XDECREF(g_pinnedGameWnd);                         // release the previous window's pin (frees if unused)
	g_pinnedGameWnd = (PyObject*)cur;
}


// ===================== RUNTIME TOGGLES (debug scaffolding) =====================================
// Bisect eXLib's always-on subsystems WITHOUT a rebuild or client restart: edit
//   <clientroot>\exlib_toggles.txt      (one "key=0|1" per line, '#' comments ok)
// and it takes effect within ~2s (re-read every TOGGLE_POLL frames from the process loop).
// Missing file / missing key = 1 = current shipping behavior, so an absent file changes nothing.
// Keys:
//   gil_guard      1 = hold the GIL around __AppProcess's python work (the world-reload crash fix)
//   app_python     1 = do ANY python work in __AppProcess (map name, phase replay, autohunt pump)
//   app_autohunt   1 = per-frame callAutoHunting("OnUpdate")
//   app_reentry    1 = world re-entry branch (reloadGamePhase + replayGamePhase + onReenter)
//   packet_gil     1 = hold the GIL in the packet path (NetworkStream CheckPacket)
// Applied values are echoed to <clientroot>\exlib_toggles.log on every change, so we can confirm
// a toggle actually landed rather than guessing.
struct ExToggles {
	bool gil_guard, app_python, app_autohunt, app_reentry, packet_gil, checkpacket, recover_getattr;
	ExToggles() : gil_guard(true), app_python(true), app_autohunt(true), app_reentry(true), packet_gil(true), checkpacket(true), recover_getattr(true) {}
	bool operator!=(const ExToggles& o) const {
		return gil_guard != o.gil_guard || app_python != o.app_python || app_autohunt != o.app_autohunt
			|| app_reentry != o.app_reentry || packet_gil != o.packet_gil || checkpacket != o.checkpacket
			|| recover_getattr != o.recover_getattr;
	}
};
static ExToggles g_tg;
static int g_tgPoll = 0;
static const int TOGGLE_POLL = 120;   // frames between re-reads (~2s)

bool ExToggle_PacketGil() { return g_tg.packet_gil; }     // used by NetworkStream.cpp
bool ExToggle_RecoverGetattr() { return g_tg.recover_getattr; }  // used by main.cpp -- 0 disables the VEH
                                                          // getattr-UAF recovery (to verify the pin alone holds)
bool ExToggle_CheckPacket() { return g_tg.checkpacket; }  // used by NetworkStream.cpp -- 0 skips the packet-hook
                                                          // python processing (InstancesList + callbacks) to
                                                          // bisect the teleport UAF. Entity detection stops when 0.

static void reloadToggles() {
	std::string path(getDllPath());
	path += "exlib_toggles.txt";
	FILE* f = fopen(path.c_str(), "r");
	if (!f)
		return;                     // no file -> keep defaults (ship behavior)
	ExToggles t;                    // start from defaults so a removed line reverts to ON
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		if (line[0] == '#' || line[0] == ';')
			continue;
		char key[128]; int val = 1;
		if (sscanf(line, " %127[^= \t] = %d", key, &val) == 2) {
			std::string k(key);
			bool b = (val != 0);
			if      (k == "gil_guard")    t.gil_guard    = b;
			else if (k == "app_python")   t.app_python   = b;
			else if (k == "app_autohunt") t.app_autohunt = b;
			else if (k == "app_reentry")  t.app_reentry  = b;
			else if (k == "packet_gil")   t.packet_gil   = b;
			else if (k == "checkpacket")  t.checkpacket  = b;
			else if (k == "recover_getattr") t.recover_getattr = b;
		}
	}
	fclose(f);
	g_tg = t;   // silent apply; the file is an optional override, defaults ship ON. No debug log written.
}
// ===============================================================================================


// Call a no-arg method on OpenBot.Modules.AutoHunting.instance. The client drops the python
// ScriptWindow.OnUpdate pump on world reload (teleport), so we drive the bot's Frame from this
// process-loop hook instead (it always runs). Read the module straight from sys.modules (borrowed) --
// NOT PyImport_ImportModule, which can run import machinery / re-execute during the phase transition.
static void callAutoHunting(const char* method) {
	PyObject* modules = PyImport_GetModuleDict();   // borrowed
	if (!modules) return;
	PyObject* mod = PyDict_GetItemString(modules, "OpenBot.Modules.AutoHunting");   // borrowed
	if (!mod) return;
	PyObject* inst = PyObject_GetAttrString(mod, "instance");   // new ref
	if (!inst) { PyErr_Clear(); return; }
	CallMethod(inst, method, "");   // self-contained, no manual refcount (see PythonUtils.h)
	Py_DECREF(inst);
	if (PyErr_Occurred()) PyErr_Clear();
}


bool CApp::__AppProcess(ClassPointer p) {
	CMemory& memory = CMemory::Instance();

	if (!passed) {
		passed = true;
		initMainThread();
		DEBUG_INFO_LEVEL_1("Main Objects loaded!");
	}

	// walker build: packet hooks are off, so the client's phase machinery never reaches uBot.
	// isInGame() reads the real client map name, so it correctly flips false during a teleport/map
	// load and true again once the new map is ready. We use that edge to (1) run script.py once on
	// first entry, and (2) on EVERY subsequent world re-entry, reload the collision map for the new
	// map and replay the game-phase event to python -- otherwise CURRENT_PHASE stays stale, the
	// bots' Frame pump never resumes, and the walker keeps the previous map's collision. This is the
	// process-loop hook (runs every frame regardless of UI state), the only place that can recover it.
	// ---------------------------------------------------------------------------------------------------
	// GIL GUARD -- fixes the world-reload heap/GC corruption crash.
	// Everything below touches the Python C-API (currentMapName -> CallMethodRetStr, executePythonFile,
	// forceGamePhase/reloadGamePhase, PyImport_*, PyObject_GetAttrString, CallMethod, Py_DECREF). This is the
	// client's PROCESS-LOOP hook: it runs on the game thread, which does NOT hold the GIL, while uBot runs
	// background Python threads (OpenThreads: thread.start_new_thread x3). Unguarded C-API calls from here
	// race those threads on refcounts -> an object gets freed while still linked in a GC generation ->
	// corrupted gc_next -> the crash in update_refs (python27+0x31B16) on the next collection, which a world
	// reload reliably triggers. The packet path (NetworkStream.cpp:323) was already guarded; this was not.
	// PyGILState_Ensure is recursive-safe, so it's fine even if we're already called with the GIL held.
	// ---------------------------------------------------------------------------------------------------
	if (++g_tgPoll >= TOGGLE_POLL) { g_tgPoll = 0; reloadToggles(); }   // live bisect, no restart

	if (!Py_IsInitialized() || !g_tg.app_python) {
		CCommunication& c0 = CCommunication::Instance();
		c0.Process();
		return memory.callProcess(p);
	}
	PyGILState_STATE __gil;
	if (g_tg.gil_guard) __gil = PyGILState_Ensure();

	pinGameWindow();   // ROOT fix: keep the borrowed m_apoPhaseWnd[GAME] window alive (see note above)

	CBackground& bck = CBackground::Instance();
	std::string curMap = bck.currentMapName();   // "" when not in the game world
	bool inGame = !curMap.empty();

	if (!mainScriptExec && passed) {
		if (inGame) {
			CNetworkStream& net = CNetworkStream::Instance();
			net.forceGamePhase();
			mainScriptExec = true;
			wasInGame = true;
			lastMap = curMap;
			executePythonFile("init.py");   // merged loader lives in init.py (phase-guarded); was script.py
			pinAllModules();                // immortalize every loaded module (see note above)
		}
	}
	else if (g_tg.app_reentry && mainScriptExec && inGame && (!wasInGame || curMap != lastMap)) {
		// World re-entry: a portal warp changes the map name WITHOUT an empty gap (so the isInGame
		// edge alone misses it -> we also fire on any map-name change), while a channel switch drops
		// out of game first (caught by the !wasInGame edge). Reload collision for the new map and
		// replay PHASE_GAME to python (refresh Hooks.CURRENT_PHASE + re-fire callbacks) so the bots'
		// Frame pump resumes -- this is the only per-frame hook that survives a world reload.
		CNetworkStream& net = CNetworkStream::Instance();
		net.reloadGamePhase();
		PyObject* hooks = PyImport_ImportModule("Hooks");
		if (hooks) {
			CallMethod(hooks, "replayGamePhase", "");   // self-contained (see PythonUtils.h)
			Py_DECREF(hooks);
		}
		callAutoHunting("onReenter");   // AutoHunting lives in the package Hooks the replay can't reach
		pinAllModules();                // pin any modules imported lazily since first load (idempotent)
	}
	if (inGame)
		lastMap = curMap;
	wasInGame = inGame;

	// Drive AutoHunting's Frame once script.py is loaded (the client's OnUpdate pump is dropped for
	// ~1min after a world reload). NOT gated on inGame here -- the client map name can lag on landing,
	// and OnUpdate self-throttles, gates on State(STOPPED), and Frame's own _inGame() guard bails when
	// not in the world. Harmless double-drive when the client pump is also alive (throttle dedups).
	if (g_tg.app_autohunt && mainScriptExec)
		callAutoHunting("OnUpdate");

	if (g_tg.gil_guard) PyGILState_Release(__gil);   // end GIL-guarded region (see note above)

	CCommunication& c = CCommunication::Instance();
	c.Process();

	return memory.callProcess(p);
}

void CApp::init() {
#ifdef _DEBUG
	SetupConsole();
	setDebugStreamFiles();
	DEBUG_INFO_LEVEL_1("Dll Loaded From %s", getDllPath());
#endif

#ifdef _DEBUG_FILE
	setDebugStreamFiles();
	SetupDebugFile();
#endif
	static CBackground bck = CBackground();
	static CCommunication coms; //Weird compile error with constructor
	static CInstanceManager mgr = CInstanceManager();
	static CPlayer pl = CPlayer();
	static CNetworkStream ns = CNetworkStream();
	static CMemory memory = CMemory();

	DEBUG_INFO_LEVEL_1("Loaded Objects");

#ifdef GET_ADDRESS_FROM_SERVER
	if (!coms.MainServerSetAuthKey()) {
		MessageBoxA(NULL, "Error while connecting to the server.", "Authentication error", MB_OK);
		return;
	}
	DEBUG_INFO_LEVEL_1("Authentication was sucessfull");
#endif

	memory.setupPatterns(hDll);
#ifdef _DEBUG
	system("pause");
#else
	//Sleep(1000);
#endif

	DEBUG_INFO_LEVEL_1("Patterns have been set sucessfully");
	if (memory.setupProcessHook()) {
		DEBUG_INFO_LEVEL_1("Process Hook sucessfull");
	}
	else {
		DEBUG_INFO_LEVEL_1("Error on process hook");
	}
}

void CApp::initMainThread() {
	CMemory& memory = CMemory::Instance();
	initModule();
	// Ensure the Python GIL machinery exists before any hook that touches the Python
	// C-API from the packet path. eXLib reenters Python from CheckPacket while uBot
	// runs background Python threads; PyGILState_Ensure (used to guard those calls) is
	// only safe once the GIL has been created. Idempotent / no-op if already inited.
	PyEval_InitThreads();
	initPythonModules();
	memory.setupHooks();
}

void CApp::SetupConsole()
{
	AllocConsole();
	freopen("CONOUT$", "wb", stdout);
	freopen("CONOUT$", "wb", stderr);
	freopen("CONIN$", "rb", stdin);
	SetConsoleTitle("Debug Console");

}

void CApp::SetupDebugFile()
{
	std::string log_path(getDllPath());
	log_path += "ex_log.txt";
	freopen(log_path.c_str(), "wb", stdout);
	freopen(log_path.c_str(), "wb", stderr);
}


void CApp::initPythonModules() {
	CBackground& bck = CBackground::Instance();
	CInstanceManager& mgr = CInstanceManager::Instance();
	CPlayer& pl = CPlayer::Instance();
	CNetworkStream& ns = CNetworkStream::Instance();
	
	bck.importPython();
	mgr.importPython();
	pl.importPython();
	ns.importPython();
}

void CApp::exit() {
	DEBUG_INFO_LEVEL_1("LEAVING!");
	/*CMemory& memory = CMemory::Instance();
	CBackground& bck = CBackground::Instance();
	CInstanceManager& mgr = CInstanceManager::Instance();
	CPlayer& pl = CPlayer::Instance();
	CNetworkStream& ns = CNetworkStream::Instance();
	memory.~CMemory();
	bck.~CBackground();
	mgr.~CInstanceManager();
	pl.~CPlayer();
	ns.~CNetworkStream();*/

	fclose(stdin);
	fclose(stdout);
	fclose(stderr);
	FreeConsole();
}

void CApp::setSkipRenderer()
{
	CMemory& mem = CMemory::Instance();
	mem.setSkipRenderer();
}

void CApp::unsetSkipRenderer()
{
	CMemory& mem = CMemory::Instance();
	mem.unsetSkipRenderer();
}

CApp::CApp()
{

	mainScriptExec = false;
	passed = false;
	wasInGame = false;

}

CApp::~CApp()
{
	exit();
}
