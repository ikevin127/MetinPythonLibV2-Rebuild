#pragma once
#include "stdafx.h"
#include <string>
#include <iostream>
#include <set>
#define SET_EXCEPTION(x) PyErr_SetString(PyExc_RuntimeError, #x)


bool PyTuple_GetIntSet(PyObject* poArgs, int pos, std::set<DWORD>* arr);
bool PyTuple_GetByteArray(PyObject* poArgs, int pos, BYTE** arr); //CALLER IS RESPONSIBLE FOR FREEING MEMORY
bool PyTuple_GetString(PyObject* poArgs, int pos, char** ret);
bool PyTuple_GetInteger(PyObject* poArgs, int pos, unsigned char* ret);
bool PyTuple_GetInteger(PyObject* poArgs, int pos, int* ret);
bool PyTuple_GetInteger(PyObject* poArgs, int pos, WORD* ret);
bool PyTuple_GetByte(PyObject* poArgs, int pos, unsigned char* ret);
bool PyTuple_GetUnsignedInteger(PyObject* poArgs, int pos, unsigned int* ret);
bool PyTuple_GetLong(PyObject* poArgs, int pos, long* ret);
bool PyTuple_GetUnsignedLong(PyObject* poArgs, int pos, unsigned long* ret);
bool PyTuple_GetFloat(PyObject* poArgs, int pos, float* ret);
bool PyTuple_GetDouble(PyObject* poArgs, int pos, double* ret);
bool PyTuple_GetObject(PyObject* poArgs, int pos, PyObject** ret);
bool PyTuple_GetBoolean(PyObject* poArgs, int pos, bool* ret);

bool PyCallClassMemberFunc(PyObject* poClass, const char* c_szFunc, PyObject* poArgs);
bool PyCallClassMemberFunc(PyObject* poClass, const char* c_szFunc, PyObject* poArgs, bool* pisRet);
bool PyCallClassMemberFunc(PyObject* poClass, const char* c_szFunc, PyObject* poArgs, long * plRetValue);
bool PyCallClassMemberFunc(PyObject* poClass, const char* c_szFunc, PyObject* poArgs, std::string& str);

bool PyCallClassMemberFunc_ByPyString(PyObject* poClass, PyObject* poFuncName, PyObject* poArgs);
bool PyCallClassMemberFunc(PyObject* poClass, PyObject* poFunc, PyObject* poArgs);

// ============================================================================================
// PREFER THESE for any new eXLib->Python call. They are self-contained: they build the args from
// (fmt, ...), call obj.method(*args), and release EVERY PyObject (fn, args, result) INTERNALLY --
// the caller never touches a refcount, so the double-free/double-decref crash class (empty-tuple
// UAF etc.) is structurally impossible. Pass "" as fmt for a no-arg call. Do NOT hand-build a tuple
// or pair these with Py_BuildValue -- that reintroduces the manual-refcount foot-gun the raw
// PyCallClassMemberFunc has.
//   CallMethod(obj, "Method", "");                 // obj.Method()
//   CallMethod(obj, "Method", "(iii)", a, b, c);   // obj.Method(a, b, c)  (result discarded)
//   long v; CallMethodRetLong(obj, "M", &v, "(s)", name);
//   std::string s; CallMethodRetStr(obj, "GetName", s, "");
// ============================================================================================
bool CallMethod(PyObject* obj, const char* method, const char* fmt, ...);
bool CallMethodRetLong(PyObject* obj, const char* method, long* out, const char* fmt, ...);
bool CallMethodRetStr(PyObject* obj, const char* method, std::string& out, const char* fmt, ...);

PyObject * Py_BuildException(const char * c_pszErr = NULL, ...);
PyObject * Py_BadArgument();
PyObject * Py_BuildNone();
PyObject * Py_BuildEmptyTuple();
