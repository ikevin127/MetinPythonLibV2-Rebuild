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

HMODULE hDll = 0;


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
	// PyCallClassMemberFunc consumes poArgs (Py_XDECREFs it) -- do NOT decref again (double-free).
	PyObject* poArgs = Py_BuildValue("()");
	PyCallClassMemberFunc(inst, method, poArgs);
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
		}
	}
	else if (mainScriptExec && inGame && (!wasInGame || curMap != lastMap)) {
		// World re-entry: a portal warp changes the map name WITHOUT an empty gap (so the isInGame
		// edge alone misses it -> we also fire on any map-name change), while a channel switch drops
		// out of game first (caught by the !wasInGame edge). Reload collision for the new map and
		// replay PHASE_GAME to python (refresh Hooks.CURRENT_PHASE + re-fire callbacks) so the bots'
		// Frame pump resumes -- this is the only per-frame hook that survives a world reload.
		CNetworkStream& net = CNetworkStream::Instance();
		net.reloadGamePhase();
		PyObject* hooks = PyImport_ImportModule("Hooks");
		if (hooks) {
			PyObject* poArgs = Py_BuildValue("()");
			PyCallClassMemberFunc(hooks, "replayGamePhase", poArgs);  // consumes poArgs (no extra decref)
			Py_DECREF(hooks);
		}
		callAutoHunting("onReenter");   // AutoHunting lives in the package Hooks the replay can't reach
	}
	if (inGame)
		lastMap = curMap;
	wasInGame = inGame;

	// Drive AutoHunting's Frame once script.py is loaded (the client's OnUpdate pump is dropped for
	// ~1min after a world reload). NOT gated on inGame here -- the client map name can lag on landing,
	// and OnUpdate self-throttles, gates on State(STOPPED), and Frame's own _inGame() guard bails when
	// not in the world. Harmless double-drive when the client pump is also alive (throttle dedups).
	if (mainScriptExec)
		callAutoHunting("OnUpdate");

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
