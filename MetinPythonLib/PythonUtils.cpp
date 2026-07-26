#include "stdafx.h"
#include "PythonUtils.h"

bool __PyCallClassMemberFunc_ByCString(PyObject* poClass, const char* c_szFunc, PyObject* poArgs, PyObject** poRet);
bool __PyCallClassMemberFunc_ByPyString(PyObject* poClass, PyObject* poFuncName, PyObject* poArgs, PyObject** poRet);
bool __PyCallClassMemberFunc(PyObject* poClass, PyObject* poFunc, PyObject* poArgs, PyObject** poRet);

// ---- Self-contained method-call helpers (see PythonUtils.h). Own EVERY PyObject internally so a caller
// can never double-free the args -> the empty-tuple UAF / GC-corruption crash class is impossible here.
static PyObject* __BuildCallArgs(const char* fmt, va_list ap)
{
	if (!fmt || fmt[0] == '\0')
		return PyTuple_New(0);              // our own ref to the empty tuple; balanced by the caller's decref
	PyObject* a = Py_VaBuildValue(fmt, ap); // new ref
	if (!a)
		return NULL;
	if (!PyTuple_Check(a)) {                 // a single non-tuple value -> wrap it in a 1-tuple
		PyObject* t = PyTuple_New(1);
		PyTuple_SetItem(t, 0, a);           // steals a's ref
		return t;
	}
	return a;
}

static PyObject* __CallMethodRaw(PyObject* obj, const char* method, const char* fmt, va_list ap)
{
	if (!obj)
		return NULL;
	PyObject* fn = PyObject_GetAttrString(obj, method);   // new ref
	if (!fn) { PyErr_Clear(); return NULL; }
	if (!PyCallable_Check(fn)) { Py_DECREF(fn); return NULL; }
	PyObject* args = __BuildCallArgs(fmt, ap);
	if (!args) { Py_DECREF(fn); PyErr_Clear(); return NULL; }
	PyObject* ret = PyObject_CallObject(fn, args);        // new ref or NULL
	Py_DECREF(args);
	Py_DECREF(fn);
	if (!ret) { PyErr_Clear(); return NULL; }
	return ret;   // caller decrefs
}

bool CallMethod(PyObject* obj, const char* method, const char* fmt, ...)
{
	va_list ap; va_start(ap, fmt);
	PyObject* ret = __CallMethodRaw(obj, method, fmt, ap);
	va_end(ap);
	if (!ret) return false;
	Py_DECREF(ret);
	return true;
}

bool CallMethodRetLong(PyObject* obj, const char* method, long* out, const char* fmt, ...)
{
	va_list ap; va_start(ap, fmt);
	PyObject* ret = __CallMethodRaw(obj, method, fmt, ap);
	va_end(ap);
	if (!ret) return false;
	if (out) {
		if (PyInt_Check(ret))       *out = PyInt_AsLong(ret);
		else if (PyLong_Check(ret)) *out = PyLong_AsLong(ret);
		else if (PyBool_Check(ret)) *out = (ret == Py_True) ? 1 : 0;
		else                        *out = 0;
	}
	Py_DECREF(ret);
	return true;
}

bool CallMethodRetStr(PyObject* obj, const char* method, std::string& out, const char* fmt, ...)
{
	va_list ap; va_start(ap, fmt);
	PyObject* ret = __CallMethodRaw(obj, method, fmt, ap);
	va_end(ap);
	if (!ret) return false;
	if (PyString_Check(ret)) {
		const char* s = PyString_AsString(ret);
		out = s ? s : "";
	}
	Py_DECREF(ret);
	return true;
}

PyObject * Py_BadArgument()
{
	PyErr_BadArgument();
	return NULL;
}

PyObject * Py_BuildException(const char * c_pszErr, ...)
{
	if (!c_pszErr)
		PyErr_Clear();
	else
	{
		char szErrBuf[512 + 1];
		va_list args;
		va_start(args, c_pszErr);
		vsnprintf(szErrBuf, sizeof(szErrBuf), c_pszErr, args);
		va_end(args);

		PyErr_SetString(PyExc_RuntimeError, szErrBuf);
	}

	return Py_BuildNone();
	//return NULL;
}

PyObject * Py_BuildNone()
{
	Py_INCREF(Py_None);
	return Py_None;
}

void Py_ReleaseNone()
{
	Py_DECREF(Py_None);
}

bool PyTuple_GetObject(PyObject* poArgs, int pos, PyObject** ret)
{
	if (pos >= PyTuple_Size(poArgs))
		return false;

	PyObject * poItem = PyTuple_GetItem(poArgs, pos);
	if (!poItem)
		return false;

	Py_INCREF(poItem);
	*ret = poItem;
	return true;
}

bool PyTuple_GetLong(PyObject* poArgs, int pos, long* ret)
{
	if (pos >= PyTuple_Size(poArgs))
		return false;

	PyObject* poItem = PyTuple_GetItem(poArgs, pos);

	if (!poItem)
		return false;

	*ret = PyLong_AsLong(poItem);
	return true;
}

bool PyTuple_GetDouble(PyObject* poArgs, int pos, double* ret)
{
	if (pos >= PyTuple_Size(poArgs))
		return false;

	PyObject* poItem = PyTuple_GetItem(poArgs, pos);

	if (!poItem)
		return false;

	*ret = PyFloat_AsDouble(poItem);
	return true;
}

bool PyTuple_GetFloat(PyObject* poArgs, int pos, float* ret)
{
	if (pos >= PyTuple_Size(poArgs))
		return false;

	PyObject * poItem = PyTuple_GetItem(poArgs, pos);

	if (!poItem)
		return false;

	*ret = float(PyFloat_AsDouble(poItem));
	return true;
}

bool PyTuple_GetByte(PyObject* poArgs, int pos, unsigned char* ret)
{
	int val;
	bool result = PyTuple_GetInteger(poArgs, pos, &val);
	*ret = unsigned char(val);
	return result;
}

bool PyTuple_GetInteger(PyObject* poArgs, int pos, unsigned char* ret)
{
	int val;
	bool result = PyTuple_GetInteger(poArgs, pos, &val);
	*ret = unsigned char(val);
	return result;
}

bool PyTuple_GetInteger(PyObject* poArgs, int pos, WORD* ret)
{
	int val;
	bool result = PyTuple_GetInteger(poArgs, pos, &val);
	*ret = WORD(val);
	return result;
}

bool PyTuple_GetInteger(PyObject* poArgs, int pos, int* ret)
{
	if (pos >= PyTuple_Size(poArgs))
		return false;

	PyObject* poItem = PyTuple_GetItem(poArgs, pos);

	if (!poItem)
		return false;

	*ret = PyLong_AsLong(poItem);
	return true;
}

bool PyTuple_GetUnsignedLong(PyObject* poArgs, int pos, unsigned long* ret)
{
	if (pos >= PyTuple_Size(poArgs))
		return false;

	PyObject * poItem = PyTuple_GetItem(poArgs, pos);

	if (!poItem)
		return false;

	*ret = PyLong_AsUnsignedLong(poItem);
	return true;
}

bool PyTuple_GetUnsignedInteger(PyObject* poArgs, int pos, unsigned int* ret)
{
	if (pos >= PyTuple_Size(poArgs))
		return false;

	PyObject* poItem = PyTuple_GetItem(poArgs, pos);

	if (!poItem)
		return false;

	*ret = PyLong_AsUnsignedLong(poItem);
	return true;
}


bool PyTuple_GetByteArray(PyObject * poArgs, int pos, BYTE ** arr)
{
	if (pos >= PyTuple_Size(poArgs))
		return false;

	PyObject* poItem = PyTuple_GetItem(poArgs, pos);

	if (!poItem)
		return false;

	if (!PyByteArray_Check(poItem))
		return false;

	*arr = (BYTE*)PyByteArray_AsString(poItem);
	return true;
}

bool PyTuple_GetString(PyObject* poArgs, int pos, char** ret)
{
	if (pos >= PyTuple_Size(poArgs))
		return false;

	PyObject* poItem = PyTuple_GetItem(poArgs, pos);

	if (!poItem)
		return false;

	if (!PyString_Check(poItem))
		return false;

	*ret = PyString_AsString(poItem);
	return true;
}

bool PyTuple_GetBoolean(PyObject* poArgs, int pos, bool* ret)
{
	if (pos >= PyTuple_Size(poArgs))
		return false;

	PyObject* poItem = PyTuple_GetItem(poArgs, pos);

	if (!poItem)
		return false;

	*ret = PyLong_AsLong(poItem) ? true : false;
	return true;
}

bool PyCallClassMemberFunc(PyObject* poClass, PyObject* poFunc, PyObject* poArgs)
{
	PyObject* poRet;

	// NOTE : NULL üũ �߰�.. - [levites]
	if (!poClass)
	{
		Py_XDECREF(poArgs);
		return false;
	}

	if (!__PyCallClassMemberFunc(poClass, poFunc, poArgs, &poRet))
		return false;

	Py_DECREF(poRet);
	return true;
}


bool PyCallClassMemberFunc(PyObject* poClass, const char* c_szFunc, PyObject* poArgs)
{
	PyObject* poRet;

	// NOTE : NULL üũ �߰�.. - [levites]
	if (!poClass)
	{
		Py_XDECREF(poArgs);
		return false;
	}

	if (!__PyCallClassMemberFunc_ByCString(poClass, c_szFunc, poArgs, &poRet))
		return false;

	Py_DECREF(poRet);
	return true;
}

bool PyCallClassMemberFunc_ByPyString(PyObject* poClass, PyObject* poFuncName, PyObject* poArgs)
{
	PyObject* poRet;

	// NOTE : NULL üũ �߰�.. - [levites]
	if (!poClass)
	{
		Py_XDECREF(poArgs);
		return false;
	}

	if (!__PyCallClassMemberFunc_ByPyString(poClass, poFuncName, poArgs, &poRet))
		return false;

	Py_DECREF(poRet);
	return true;
}

bool PyCallClassMemberFunc(PyObject* poClass, const char* c_szFunc, PyObject* poArgs, bool* pisRet)
{
	PyObject* poRet;

	if (!__PyCallClassMemberFunc_ByCString(poClass, c_szFunc, poArgs, &poRet))
		return false;

	if (PyNumber_Check(poRet))
		*pisRet = (PyLong_AsLong(poRet) != 0);
	else
		*pisRet = true;

	Py_DECREF(poRet);
	return true;
}

bool PyCallClassMemberFunc(PyObject* poClass, const char* c_szFunc, PyObject* poArgs, std::string& str)
{
	PyObject* poRet;
	if (!__PyCallClassMemberFunc_ByCString(poClass, c_szFunc, poArgs, &poRet))
		return false;

	if (PyObject_TypeCheck(poRet, &PyBaseString_Type))
	{
		const char* retStr = PyString_AsString(poRet);
		str = std::string(retStr);
		Py_DECREF(poRet);
		return true;
	}

	Py_DECREF(poRet);
	return false;
}

bool PyCallClassMemberFunc(PyObject* poClass, const char* c_szFunc, PyObject* poArgs, long * plRetValue)
{
	PyObject* poRet;

	if (!__PyCallClassMemberFunc_ByCString(poClass, c_szFunc, poArgs, &poRet))
		return false;

	if (PyNumber_Check(poRet))
	{
		*plRetValue = PyLong_AsLong(poRet);
		Py_DECREF(poRet);
		return true;
	}

	Py_DECREF(poRet);
	return false;
}

/*
 *	�� �Լ��� ���� ȣ������ �ʵ��� �Ѵ�.
 *	�ε��� �ϰ� ���� ȣ���� ��쿡�� �ݵ�� false �� ���� ���� ��
 *	Py_DECREF(poArgs); �� ���ش�.
 */
bool __PyCallClassMemberFunc_ByCString(PyObject* poClass, const char* c_szFunc, PyObject* poArgs, PyObject** ppoRet)
{
	if (!poClass)
	{
		Py_XDECREF(poArgs);
		return false;
	}


	PyObject * poFunc = PyObject_GetAttrString(poClass, (char *)c_szFunc);	// New Reference


	if (!poFunc)
	{
		PyErr_Clear();
		Py_XDECREF(poArgs);
		return false;
	}


	if (!PyCallable_Check(poFunc))
	{
		Py_DECREF(poFunc);
		Py_XDECREF(poArgs);
		return false;
	}

	PyObject * poRet = PyObject_CallObject(poFunc, poArgs);	// New Reference

	if (!poRet)
	{

		PyErr_Print();


		Py_DECREF(poFunc);
		Py_XDECREF(poArgs);
		return false;
	}

	*ppoRet = poRet;

	Py_DECREF(poFunc);
	Py_XDECREF(poArgs);
	return true;
}

bool __PyCallClassMemberFunc_ByPyString(PyObject* poClass, PyObject* poFuncName, PyObject* poArgs, PyObject** ppoRet)
{
	if (!poClass)
	{
		Py_XDECREF(poArgs);
		return false;
	}

	PyObject * poFunc = PyObject_GetAttr(poClass, poFuncName);	// New Reference

	if (!poFunc)
	{
		PyErr_Clear();
		Py_XDECREF(poArgs);
		return false;
	}

	if (!PyCallable_Check(poFunc))
	{
		Py_DECREF(poFunc);
		Py_XDECREF(poArgs);
		return false;
	}

	PyObject * poRet = PyObject_CallObject(poFunc, poArgs);	// New Reference

	if (!poRet)
	{

		PyErr_Print();


		Py_DECREF(poFunc);
		Py_XDECREF(poArgs);
		return false;
	}

	*ppoRet = poRet;

	Py_DECREF(poFunc);
	Py_XDECREF(poArgs);
	return true;
}

bool __PyCallClassMemberFunc(PyObject* poClass, PyObject * poFunc, PyObject* poArgs, PyObject** ppoRet)
{
	if (!poClass)
	{
		Py_XDECREF(poArgs);
		return false;
	}

	if (!poFunc)
	{
		PyErr_Clear();
		Py_XDECREF(poArgs);
		return false;
	}

	if (!PyCallable_Check(poFunc))
	{
		Py_DECREF(poFunc);
		Py_XDECREF(poArgs);
		return false;
	}

	PyObject * poRet = PyObject_CallObject(poFunc, poArgs);	// New Reference

	if (!poRet)
	{
		PyErr_Print();
		Py_DECREF(poFunc);
		Py_XDECREF(poArgs);
		return false;
	}

	*ppoRet = poRet;

	Py_DECREF(poFunc);
	Py_XDECREF(poArgs);
	return true;
}