#include "stdafx.h"
#include "Memory.h"
#include "NetworkStream.h"
#include "Player.h"
#include "App.h"
#include "CAddressLoader.h"



bool __fastcall __GetEter(ClassPointer cp,DWORD edx,CMappedFile & file, const char* fileName, void** buffer) {
	CPlayer& instance = CPlayer::Instance();
	CMemory& mem = CMemory::Instance();
	bool val = mem.callGet(cp, file, fileName, buffer);
	instance.__GetEter(file, fileName, buffer);
	return val;
}

bool __fastcall __Process(ClassPointer p) {
	CApp& instance = CApp::Instance();
	return instance.__AppProcess(p);
}

bool __fastcall __RecvPacket(ClassPointer p, DWORD edx,int size, void* buffer) {
	CNetworkStream& net = CNetworkStream::Instance();
	CMemory& mem = CMemory::Instance();
	mem.setNetworkStream(p);
	return net.__RecvPacket(size, buffer);
}
bool __fastcall __SendPacket(ClassPointer classPointer,DWORD edx, int size, void* buffer) {
	CNetworkStream& net = CNetworkStream::Instance();
	CMemory& mem = CMemory::Instance();
	mem.setNetworkStream(classPointer);
	return net.__SendPacket(size, buffer);
}
/*bool __declspec(naked) __SendPacketJMP(int size, void* buffer) {
	__asm {
		PUSH ECX		//this_call
		PUSH[ESP + 8] //Return address
		JMP __SendPacket
	}
}*/

bool __fastcall __SendAttackPacket(ClassPointer classPointer, DWORD EDX, BYTE type, DWORD vid) {
	CNetworkStream& net = CNetworkStream::Instance();
	return net.__SendAttackPacket(type, vid);
}



bool __fastcall __SendSequencePacket(DWORD classPointer)
{
	CNetworkStream& net = CNetworkStream::Instance();
	return net.__SendSequencePacket();
}


//return 0 to ignore collisions
bool __fastcall __BackgroundCheckAdvanced(ClassPointer classPointer, DWORD EDX, void* instanceBase)
{
	CPlayer & instance = CPlayer::Instance();
	return instance.__BackgroundCheckAdvanced(classPointer,instanceBase);
}

//return 0 to ignore collisions
bool __fastcall __InstanceBaseCheckAdvanced(ClassPointer classPointer)
{
	CPlayer & instance = CPlayer::Instance();
	return instance.__InstanceBaseCheckAdvanced(classPointer);
}

bool __fastcall __SendStatePacket(ClassPointer classPointer, DWORD EDX, fPoint& pos, float rot, BYTE eFunc, BYTE uArg)
{
	CNetworkStream& net = CNetworkStream::Instance();
	return net.__SendStatePacket(pos,rot,eFunc,uArg);
}

bool __fastcall __MoveToDestPosition(ClassPointer classPointer, DWORD EDX, fPoint& pos)
{
	CPlayer & instance = CPlayer::Instance();
	return instance.__MoveToDestPosition(classPointer, pos);
}

bool __fastcall __MoveToDirection(ClassPointer classPointer, DWORD EDX, float rot)
{
	CPlayer& instance = CPlayer::Instance();
	return instance.__MoveToDirection(classPointer, rot);
}

bool __fastcall __CheckPacket(ClassPointer classPointer, DWORD EDX, BYTE * header)
{
	CNetworkStream& instance = CNetworkStream::Instance();
	CMemory& mem = CMemory::Instance();
	mem.setNetworkStream(classPointer); // walker build: use the real 'this' the game passed, not the resolved global
	return instance.__CheckPacket(header);
}

void __Tracef(const char* c_szFormat, ...) {
	va_list args;
	va_start(args, c_szFormat);
	Tracef(1, c_szFormat, args);
	CMemory & instance = CMemory::Instance();
	instance.traceFHook->originalFunction(c_szFormat, args);
	va_end(args);
}

void __Tracenf(const char* c_szFormat, ...) {
	va_list args;
	va_start(args, c_szFormat);
	Tracef(0, c_szFormat, args);
	CMemory & memory = CMemory::Instance();

	memory.tracenFHook->originalFunction(c_szFormat, args);
	va_end(args);
}

CMemory::CMemory()
{
	networkStream = 0;
}

CMemory::~CMemory()
{

#ifdef _DEBUG
	delete traceFHook;
	delete tracenFHook;
#endif // _DEBUG

	delete checkPacketHook;
	delete backgroundCheckAdvHook;
	delete instanceBaseCheckAdvHook;
	delete sendHook;
	delete recvHook;
	delete getEtherPacketHook;
	delete sendSequenceHook;
	delete sendStateHook;
	delete setMoveToDestPositionHook;
	delete setMoveToDirectionHook;
	delete getEtherPacketHook;
	delete processHook;
	delete graphicPatch;
}

bool CMemory::setupPatterns(HMODULE hDll)
{
	this->hDll = hDll;
	CAddressLoader addrLoader;
	bool value = addrLoader.setAddress(hDll);
	if (!value) {
		DEBUG_INFO_LEVEL_1("Error Setting Addresses");
		return value;
	}

	recvAddr = addrLoader.GetAddress(RECV_FUNCTION);//patternFinder->GetPatternAddress(&memory_patterns::recvFunction);
	sendAddr = addrLoader.GetAddress(SEND_FUNCTION);
	sendSequenceAddr = addrLoader.GetAddress(SENDSEQUENCE_FUNCTION);
	getEtherPackAddr = addrLoader.GetAddress(GETETHER_FUNCTION);//patternFinder->GetPatternAddress(&memory_patterns::getEtherPackFunction);
	statePacketAddr = addrLoader.GetAddress(SENDCHARACTERSTATE_FUNCTION);
	netClassPointer = (void**)addrLoader.GetAddress(NETWORKCLASS_POINTER);
	pythonPlayerPointer = (void**)addrLoader.GetAddress(PYTHONPLAYER_POINTER);
	chrMgrClassPointer = (void**)addrLoader.GetAddress(CHRACTERMANAGER_POINTER);
	moveToDestAddr = addrLoader.GetAddress(INSTANCEBASE_MOVETODEST);
	backgroundCheckAdvancingAddr = addrLoader.GetAddress(BACKGROUND_CHECKADVANCING);
	instanceCheckAdvancingAddr = addrLoader.GetAddress(INSTANCE_CHECKADVANCING);
	traceFFuncAddr = addrLoader.GetAddress(TRACEF_POINTER);
	tracenFFuncAddr = addrLoader.GetAddress(TRACENF_POINTER);
	moveToDirectionAddr = addrLoader.GetAddress(MOVETODIRECTION_FUNCTION);
	processAddr = addrLoader.GetAddress(PYTHONAPP_PROCESS);
	checkPacketAddr = addrLoader.GetAddress(CHECK_PACKET_FUNCTION);
	sendAttackPacketAddr = addrLoader.GetAddress(SENDATTACK_FUNCTION);
	skipGraphicsAddr = addrLoader.GetAddress(RENDER_MID_FUNCTION);

	//sendAttackPacketFunc = (tSendAttackPacket)addrLoader.GetAddress(SENDATTACK_FUNCTION);
	sendUseSkillBySlotFunc = (tSendUseSkillBySlot)addrLoader.GetAddress(PYTHONPLAYER_SENDUSESKILL);
	sendShootFunc = (tSendShootPacket)addrLoader.GetAddress(SENDSHOOT_FUNCTION); // alive client leaf @ clean entry
	sendUseSkillPacketFunc = (tSendUseSkillPacket)addrLoader.GetAddress(SENDUSESKILL_PACKET_FUNCTION); // alive network leaf (by skill index)
	newAttackFunc = (tNewAttack)addrLoader.GetAddress(NEW_ATTACK_FUNCTION); // CPythonPlayer::NEW_Attack (animated attack)
	localToGlobalFunc = (tLocalToGlobalPosition)addrLoader.GetAddress(LOCALTOGLOBAL_FUNCTION);
	globalToLocalFunc = (tGlobalToLocalPosition)addrLoader.GetAddress(GLOBALTOLOCAL_FUNCTION);
	peekFunc = (tPeek)addrLoader.GetAddress(PEEK_FUNCTION);

	// CEffectManager: one sig (StoneDetect Register+Create block) yields all three.
	registerEffectFunc = (tRegisterEffect)getRelativeCallAddress(addrLoader.GetAddress(EFFECT_REGISTER_FUNCTION));
	createEffectFunc = (tCreateEffect)getRelativeCallAddress(addrLoader.GetAddress(EFFECT_CREATE_FUNCTION));
	effectManagerPointer = SetClassPointer((DWORD**)addrLoader.GetAddress(EFFECT_MANAGER_POINTER));

	// CPythonEventManager singleton pointer. AOB is not viable (the 2-int-arg python-wrapper shape
	// that loads it matches ~21 funcs across modules; only the operand differs), so resolve by module
	// base + RVA. Absolute this-run 0x03741760 @ base 0x00980000 -> RVA 0x2DC1760 (ASLR-stable). Used
	// by GetDialogAnswerCount to read the current NPC-dialog option count (TEventSet.nAnswer).
	// BUGFIX: the RVA is into metin2client.exe, but hDll is eXLib.mix's OWN base (this DLL) -> hDll+RVA
	// lands ~47MB past our ~480KB module in unrelated memory and reads 0. Use the MAIN EXE base instead.
	// (Verified live via CE: exeBase+0x2DC1760 -> valid singleton whose m_EventSetVector has 6 slots.)
	eventMgrStatic = (DWORD*)((DWORD)GetModuleHandle(NULL) + 0x02DC1760);

	//peekFunc = (tPeek)getRelativeCallAddress((void*)peekFunc); // walker build: PEEK_FUNCTION is now a direct function sig (offset 0), not a call site
	globalToLocalFunc = (tGlobalToLocalPosition)getRelativeCallAddress((void*)globalToLocalFunc);
	recvAddr = getRelativeCallAddress(recvAddr);

	DEBUG_INFO_LEVEL_1("Peek relative final address: %#x", peekFunc);
	DEBUG_INFO_LEVEL_1("globalToLocal relative final address: %#x", globalToLocalFunc);
	DEBUG_INFO_LEVEL_1("recv relative final address: %#x", recvAddr);

	pythonNetwork = SetClassPointer((DWORD**)netClassPointer);
	pythonPlayer = SetClassPointer((DWORD**)pythonPlayerPointer);
	pythonChrMgr = SetClassPointer((DWORD**)chrMgrClassPointer);

	DEBUG_INFO_LEVEL_1("pythonNetwork->%#x", pythonNetwork);
	DEBUG_INFO_LEVEL_1("pythonPlayer->%#x", pythonPlayer);
	DEBUG_INFO_LEVEL_1("pythonChrMgr->%#x", pythonChrMgr);

	getInstanceClassPtr = (ClassPointer)((DWORD)*pythonChrMgr + OFFSET_CLIENT_INSTANCE_PTR_1);

	getInstanceFunc = reinterpret_cast<tGetInstancePointer>(*(DWORD*)(*(DWORD*)getInstanceClassPtr + OFFSET_CLIENT_INSTANCE_PTR_2));
	return true;
}

// Number of selectable options in the CURRENTLY-OPEN NPC dialog. Walks the CPythonEventManager singleton's
// m_EventSetVector (@ mgr+0x0C: begin@+0x00, end@+0x04) and returns the MAX TEventSet.nAnswer (@ es+0x1B0)
// across all live event sets -- the interactive menu has answers>0 while quest/info scrolls are 0, so the
// max auto-selects the menu. Verified live: a 2-option "Open Shop / Close" menu reads 2. SEH-guarded so a
// bad/absent struct returns -1 instead of crashing. (POD-only body -> __try is legal here.)
int CMemory::GetDialogAnswerCount()
{
	if (!eventMgrStatic)
		return -1;
	__try {
		DWORD mgr = *eventMgrStatic;                 // singleton `this`
		if (!mgr)
			return 0;
		DWORD* begin = *(DWORD**)(mgr + 0x0C);       // m_EventSetVector.begin
		DWORD* end   = *(DWORD**)(mgr + 0x10);       // m_EventSetVector.end
		if (!begin || end < begin)
			return 0;
		int maxN = 0, guard = 0;
		for (DWORD* p = begin; p < end && guard < 256; ++p, ++guard) {
			DWORD es = *p;
			if (es) {
				int n = *(int*)(es + 0x1B0);         // TEventSet.nAnswer
				if (n > 0 && n < 64 && n > maxN)
					maxN = n;
			}
		}
		return maxN;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -1;
	}
}

bool CMemory::setupHooks()
{
	//Hooks
	getEtherPacketHook = new DetoursHook<tGet>((tGet)getEtherPackAddr, __GetEter);
	recvHook = new DetoursHook<tRecvPacket>((tRecvPacket)recvAddr, __RecvPacket);
	sendHook = new DetoursHook<tSendPacket>((tSendPacket)sendAddr, __SendPacket);//new JMPStartFuncHook(sendAddr, __SendPacketJMP, 6, THIS_CALL);
	sendSequenceHook = new DetoursHook<tSendSequencePacket>((tSendSequencePacket)sendSequenceAddr, __SendSequencePacket);
	backgroundCheckAdvHook = new DetoursHook<tBackground_CheckAdvancing>((tBackground_CheckAdvancing)backgroundCheckAdvancingAddr, __BackgroundCheckAdvanced);
	instanceBaseCheckAdvHook = new DetoursHook<tInstanceBase_CheckAdvancing>((tInstanceBase_CheckAdvancing)instanceCheckAdvancingAddr, __InstanceBaseCheckAdvanced);
	sendStateHook = new DetoursHook<tSendStatePacket>((tSendStatePacket)statePacketAddr, __SendStatePacket);
	setMoveToDestPositionHook = new DetoursHook<tMoveToDestPosition>((tMoveToDestPosition)moveToDestAddr, __MoveToDestPosition);
	setMoveToDirectionHook = new DetoursHook<tMoveToDirection>((tMoveToDirection)moveToDirectionAddr, __MoveToDirection);
	checkPacketHook = new DetoursHook<tCheckPacket>((tCheckPacket)checkPacketAddr, __CheckPacket);
	sendAttackPacketHook = new DetoursHook<tSendAttackPacket>((tSendAttackPacket)sendAttackPacketAddr, __SendAttackPacket);

	traceFHook = new DetoursHook<tTracef>((tTracef)traceFFuncAddr, __Tracef);
	tracenFHook = new DetoursHook<tTracef>((tTracef)tracenFFuncAddr, __Tracenf);

	BYTE patch[] = { 0x90,0x90,0x0,0x0,0x0,0x0,0x0,0x0,0x1 }; //Patch for skip graphics
	graphicPatch = new CMemoryPatch(patch, "xx??????x", 9, skipGraphicsAddr);
#ifdef _DEBUG
	traceFHook->HookFunction();
	tracenFHook->HookFunction();
#endif // DEBUG
#ifdef _DEBUG_FILE

	traceFHook->HookFunction();
	tracenFHook->HookFunction();
#endif

	// ---- walker build: DO NOT install the game hooks ----
	// The packet hooks (recv/checkPacket/send/...) crash on the current GF client's packet
	// layer (CHECK_PACKET sig ambiguous, RECV/PEEK unreliable, packet structs stale) and are
	// not needed for the walker: GetPixelPosition reads memory directly, MoveToDestPosition is
	// invoked via the resolved address directly, FindPath is file-based. Leaving them
	// uninstalled lets the game's own functions run untouched. Only processHook (installed in
	// setupProcessHook) stays active as the init/script heartbeat.
	//getEtherPacketHook->HookFunction();
	//recvHook->HookFunction();
	//sendHook->HookFunction();
	//sendSequenceHook->HookFunction();
	//backgroundCheckAdvHook->HookFunction();
	//instanceBaseCheckAdvHook->HookFunction();
	//sendStateHook->HookFunction();
	//setMoveToDestPositionHook->HookFunction();
	//setMoveToDirectionHook->HookFunction();
	checkPacketHook->HookFunction(); // walker build PHASE 1: re-enabled to drive InstancesList (recv/send stay off)
	//sendAttackPacketHook->HookFunction();
	return true;
}

bool CMemory::setupProcessHook()
{
	processHook = new DetoursHook<tProcess>((tProcess)processAddr, __Process);
	return processHook->HookFunction();
}

void CMemory::setSkipRenderer()
{
	graphicPatch->patchMemory();
}

void CMemory::unsetSkipRenderer()
{
	graphicPatch->unPatchMemory();
}
