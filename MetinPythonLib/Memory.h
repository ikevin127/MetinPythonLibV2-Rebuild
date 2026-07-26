#pragma once
#include "stdafx.h"
#include "DetoursHook.h"
#include "../common/Patterns.h"
#include "ReturnHook.h"
#include "defines.h"
#include "Singleton.h"
#include "MemoryPatch.h"



class CMemory : public CSingleton<CMemory>
{
public:
	CMemory();
	~CMemory();

	bool setupPatterns(HMODULE hDll);
	bool setupHooks();

	bool setupProcessHook();

	inline ClassPointer getPythonNetwork() { return *pythonNetwork; };
	inline ClassPointer getPythonChrMgr() { return *pythonChrMgr; };
	inline ClassPointer getPythonPlayer() { return *pythonPlayer; };
	inline ClassPointer getNetworkStream() { return networkStream; };
	inline void setNetworkStream(ClassPointer val) {networkStream=val; };
	inline ClassPointer getEffectManager() { return effectManagerPointer ? *effectManagerPointer : 0; };
	int GetDialogAnswerCount();   // # options in the current NPC dialog (max TEventSet.nAnswer); 0 if none, -1 if unresolved

	//Hooked original functions  (walker build: NULL-guarded so dead-sig features no-op instead of calling NULL)
	inline bool callBackgroundCheckAdv(ClassPointer p,void* instanceBase) { if(!backgroundCheckAdvHook->originalFunction) return true; return backgroundCheckAdvHook->originalFunction(p, instanceBase);}
	inline bool callIntsanceBaseCheckAdv(ClassPointer p) { if(!instanceBaseCheckAdvHook->originalFunction) return true; return instanceBaseCheckAdvHook->originalFunction(p); }
	inline bool callSendSequence() { if(!sendSequenceHook->originalFunction) return false; return sendSequenceHook->originalFunction(getNetworkStream()); }
	inline bool callSendPacket(int size, void* buffer, ClassPointer classPointer = 0) { if (sendHook->originalFunction && getNetworkStream() && classPointer == 0) return sendHook->originalFunction(getNetworkStream(), size, buffer); return false; }//If network stream is not set, the function will not be called
	inline bool callSendStatePacket(fPoint& pos, float rot, BYTE eFunc, BYTE uArg) { if(!sendStateHook->originalFunction) return false; return sendStateHook->originalFunction(getPythonNetwork(),pos,rot,eFunc,uArg); }
	inline bool callMoveToDestPosition(ClassPointer p, fPoint& pos) { if(!setMoveToDestPositionHook->originalFunction) return false; return setMoveToDestPositionHook->originalFunction(p, pos); }
	inline bool callMoveToDirection(ClassPointer p, float rot) { if(!setMoveToDirectionHook->originalFunction) return false; return setMoveToDirectionHook->originalFunction(p, rot); }
	inline bool callProcess(ClassPointer p) { if(!processHook->originalFunction) return false; return processHook->originalFunction(p); }
	inline bool callGet(ClassPointer cp, CMappedFile& file, const char* fileName, void** buffer) { if(!getEtherPacketHook->originalFunction) return false; return getEtherPacketHook->originalFunction(cp, file, fileName, (LPCVOID*)buffer); }
	inline bool callCheckPacket(BYTE * header) { if(!checkPacketHook->originalFunction || !getNetworkStream()) return false; return checkPacketHook->originalFunction(getNetworkStream(),header); }
	inline bool callRecvPacket(int size, void* buffer) { if(!recvHook->originalFunction) return false; return recvHook->originalFunction(getNetworkStream(), size, buffer); }

	//Client functions
	// Combat leaf senders: prefer the live CPythonNetworkStream 'this' captured by __CheckPacket
	// (getNetworkStream(), proven correct) over the resolved NETWORKCLASS_POINTER global; fall back
	// to the global if a packet hasn't been seen yet. Both SendAttackPacket and SendShootPacket are
	// CPythonNetworkStream methods, so this 'this' is correct for both.
	inline bool callSendAttackPacket(BYTE type, DWORD vid) { if(!sendAttackPacketHook->originalFunction) return false; ClassPointer self = getNetworkStream() ? getNetworkStream() : getPythonNetwork(); return sendAttackPacketHook->originalFunction(self, type, vid); }
	inline bool callSendShootPacket(DWORD uSkill) { if(!sendShootFunc) return false; ClassPointer self = getNetworkStream() ? getNetworkStream() : getPythonNetwork(); return sendShootFunc(self, uSkill); }
	inline bool callSendUseSkillPacket(DWORD dwSkillIndex, DWORD dwTargetVID) { if(!sendUseSkillPacketFunc) return false; ClassPointer self = getNetworkStream() ? getNetworkStream() : getPythonNetwork(); return sendUseSkillPacketFunc(self, dwSkillIndex, dwTargetVID); }
	inline bool callGlobalToLocalPosition(long& lx, long& ly){ if(!globalToLocalFunc) return false; return globalToLocalFunc(getPythonNetwork(),lx,ly);}
	inline bool callLocalToGlobalPosition(long& lx, long& ly) { if(!localToGlobalFunc) return false; return localToGlobalFunc(getPythonNetwork(), lx, ly); }
	inline void* callGetInstancePointer(DWORD vid) { if(!getInstanceFunc) return 0; return getInstanceFunc(getInstanceClassPtr,vid); }
	inline void callSendUseSkillBySlot(DWORD dwSkillSlotIndex, DWORD dwTargetVID) { if(!sendUseSkillBySlotFunc) return; return sendUseSkillBySlotFunc(getPythonPlayer(), dwSkillSlotIndex, dwTargetVID); }
	// CPythonPlayer::NEW_Attack() -- the animated "hold Space" attack on the CPythonPlayer singleton.
	// It self-guards (NEW_GetMainActorPtr null-check) so a call outside the game world is a safe no-op.
	inline void callNewAttack() { if(!newAttackFunc) return; newAttackFunc(getPythonPlayer()); }
	// Write CPythonPlayer+0x34B44 = the attack-target VID (CE-verified: the exact field CPythonPlayer::
	// SetAttackTargetVID sets and NEW_Attack reads). python player.SetTarget does NOT set it, so NEW_Attack
	// had no target -> attacked forward. Setting it makes NEW_Attack do its own native move-to-attack +
	// smooth face + attack on our mob. Offset is a struct offset (ASLR-stable); guarded null.
	inline void setAttackTargetVID(DWORD vid) { ClassPointer p = getPythonPlayer(); if(p) *(DWORD*)(p + 0x34B44) = vid; }
	inline bool callPeek(int len, void*buffer) { if(!peekFunc) return false; return peekFunc(getNetworkStream(),len,buffer); }
	//CEffectManager (main-thread / D3D; only call from the python / App::Process path). NULL-guarded so a missing sig no-ops.
	inline bool callRegisterEffect(const char* fileName) { if(!registerEffectFunc || !getEffectManager()) return false; return registerEffectFunc(getEffectManager(), fileName, false, false) != 0; }
	inline int callCreateEffect(const char* fileName, fPoint3D* pos, fPoint3D* rot) { if(!createEffectFunc || !getEffectManager()) return -1; return createEffectFunc(getEffectManager(), fileName, pos, rot); }

	void setSkipRenderer();
	void unsetSkipRenderer();

	DetoursHook<tTracef>* traceFHook;
	DetoursHook<tTracef>* tracenFHook;

private:
	inline ClassPointer* SetClassPointer(DWORD** pointer) { if(pointer)return (ClassPointer*)*pointer; };

	

private:

	//Hooks
	DetoursHook<tRecvPacket>* recvHook;

	DetoursHook<tGet> * getEtherPacketHook;
	DetoursHook<tBackground_CheckAdvancing>* backgroundCheckAdvHook;
	DetoursHook<tInstanceBase_CheckAdvancing>* instanceBaseCheckAdvHook;
	DetoursHook<tSendSequencePacket>* sendSequenceHook;
	DetoursHook<tSendPacket>* sendHook;
	DetoursHook<tSendStatePacket>* sendStateHook;
	DetoursHook<tMoveToDestPosition>* setMoveToDestPositionHook;
	DetoursHook<tMoveToDirection>* setMoveToDirectionHook;
	DetoursHook<tProcess>* processHook;
	DetoursHook<tCheckPacket>* checkPacketHook;
	DetoursHook<tSendAttackPacket>* sendAttackPacketHook;

	//Memory patches
	CMemoryPatch* graphicPatch;

	//Pointers
	void* recvAddr;
	void* sendAddr;
	void* sendSequenceAddr;
	void* getEtherPackAddr;
	void* statePacketAddr;
	void** netClassPointer;
	void** pythonPlayerPointer;
	void** chrMgrClassPointer;
	void* moveToDestAddr;
	void* backgroundCheckAdvancingAddr;
	void* instanceCheckAdvancingAddr;
	void* traceFFuncAddr;
	void* tracenFFuncAddr;
	void* moveToDirectionAddr;
	void* processAddr;
	void* checkPacketAddr;
	void* sendAttackPacketAddr;
	void* skipGraphicsAddr;

	tLocalToGlobalPosition	localToGlobalFunc;
	tGlobalToLocalPosition	globalToLocalFunc;
	//tSendAttackPacket		sendAttackPacketFunc;
	tGetInstancePointer		getInstanceFunc;
	tSendUseSkillBySlot		sendUseSkillBySlotFunc;
	tSendShootPacket		sendShootFunc;
	tSendUseSkillPacket		sendUseSkillPacketFunc;
	tNewAttack				newAttackFunc;
	tPeek					peekFunc;
	tRegisterEffect			registerEffectFunc;
	tCreateEffect			createEffectFunc;
	ClassPointer*			effectManagerPointer;
	DWORD*					eventMgrStatic;   // CPythonEventManager singleton pointer (module base + RVA 0x2DC1760)

	ClassPointer* pythonNetwork;
	ClassPointer* pythonChrMgr;
	ClassPointer* pythonPlayer;
	ClassPointer networkStream; //Used by Send
	ClassPointer getInstanceClassPtr;


	HMODULE hDll;
};

