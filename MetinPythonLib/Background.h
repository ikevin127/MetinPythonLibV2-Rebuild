#pragma once
#include "Singleton.h"
#include "MapCollision.h"
#include "../common/utils.h"
#include "PythonUtils.h"
class CBackground : public CSingleton<CBackground>
{

public:
	CBackground();
	~CBackground();

	void importPython();

	//REAL COORDINATES FROM PYTHON FUNCTIONS NEEDS TO BE DIVIDED BY 100
	//BEFORE PASSING TO THE NEXT FUNCTIONS
	bool setCurrentCollisionMap();
	void freeCurrentMap();

	bool isBlockedPosition(int x, int y);
	BYTE getAttrByte(int x, int y);
	bool findPath(int x_start, int y_start, int x_end, int y_end, std::vector<Point>& path);

	bool isPathBlocked(int x_start, int y_start, int x_end, int y_end); //USE GAME COORDS
	bool getClosestUnblocked(int x_start, int y_start, Point* buffer);
	bool isInGame(); //walker build: in-game detector via GetCurrentMapName (replaces packet-based phase tracking)
	std::string currentMapName(); //walker build: current client map name ("" if not in game) -- used to detect world re-entry
private:

	MapCollision* currMap;
	PyObject* background_mod;
};

