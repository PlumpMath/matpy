/* Copyright (c) 2012, STANISLAW ADASZEWSKI
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of STANISLAW ADASZEWSKI nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL STANISLAW ADASZEWSKI BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#include <mex.h>
#include <Python.h>
#include <string.h>
#include <dlfcn.h>

static const char *pyObjectToString(PyObject *pyObject);
static void addVariableToPython(const char* name, PyObject *value);
static PyObject *matpy_write(PyObject *self, PyObject *args);
static PyObject* matpy_flush(PyObject* self, PyObject* args);
static void initMatpyPrint(void);

static PyObject *globals;
static PyObject *module;
static int nlhs, nrhs;
static mxArray **plhs;
static const mxArray **prhs;
static PyObject *np_array_fun;
static PyObject *ndarray_cls;
static bool debug = false;

static PyMethodDef matpyPrintMethods[] =
{
    {"write", matpy_write, METH_VARARGS, "write is used to output to the MATLAB console"},
    {"flush", matpy_flush, METH_VARARGS, "flush is not needed and does nothing"},
    {NULL, NULL, 0, NULL}
};

static PyObject *matpy_write(PyObject *self, PyObject *args)
{
    const char *output;
    if (!PyArg_ParseTuple(args, "s", &output))
        return NULL;
    printf("%s", output);
    return Py_BuildValue("");
}

static PyObject* matpy_flush(PyObject* self, PyObject* args)
{
    return Py_BuildValue("");
}

static void initMatpyPrint(void)
{
    PyObject *matpyPrintModule = Py_InitModule("print", matpyPrintMethods);
    if (NULL != matpyPrintModule)
    {
        PySys_SetObject((char*)"stdout", matpyPrintModule);
        PySys_SetObject((char*)"stderr", matpyPrintModule);
    }
}

static PyObject* mat2py(const mxArray *a) {
	size_t ndims = mxGetNumberOfDimensions(a);
	const mwSize *dims = mxGetDimensions(a);
	mxClassID cls = mxGetClassID(a);
	size_t nelem = mxGetNumberOfElements(a);
	char *data = (char*) mxGetData(a);
	char *imagData = (char*) mxGetImagData(a);

	if (debug) mexPrintf("cls = %d, nelem = %d, ndims = %d, dims[0] = %d, dims[1] = %d\n", cls, nelem, ndims, dims[0], dims[1]);

	if (cls == mxCHAR_CLASS) {
		char *str = mxArrayToString(a);
		PyObject *o = PyString_FromString(str);
		mxFree(str);
		return o;
	} else if (mxIsStruct(a)) {
		PyObject *o = PyDict_New();
		PyObject *list;
		PyObject *pyItem;
		mxArray *item;
		int i, j;
		const char *fieldName;

		int nfields = mxGetNumberOfFields(a);
		if (debug) mexPrintf("nfields = %d, nelem = %d\n", nfields, nelem);

		for(i = 0; i < nfields; i++) {
			fieldName = mxGetFieldNameByNumber(a, i);
			list = PyList_New(nelem);

			for(j = 0; j < nelem; j++) {
				item = mxGetFieldByNumber(a, j, i);
                if(item == NULL)
                {
                	Py_DECREF(list);
                	Py_DECREF(o);
                	mexErrMsgIdAndTxt("matpy:NullFieldValue", "Null field in struct");
                }
				pyItem = mat2py(item);
                if(pyItem == NULL)
                {
                	Py_DECREF(list);
                	Py_DECREF(o);
                    mexErrMsgIdAndTxt("matpy:UnsupportedVariableType", "Unsupported variable type in struct");
                }
    			
                PyList_SetItem(list, j, pyItem);
            }

            PyDict_SetItemString(o, fieldName, list);
		}

		Py_DECREF(list);

		return o;
	}

	PyObject *list = PyList_New(nelem);
	const char *dtype = NULL;
	if (mxIsCell(a)) 
	{
		dtype = "object";

		for (int i = 0; i < nelem; i++) 
		{
			PyObject *item = mat2py(mxGetCell(a, i));

			if(NULL != item)
			{
				PyList_SetItem(list, i, item);
			}
			else // Failure
			{
				Py_DECREF(list);
				mexErrMsgIdAndTxt("matpy:UnsupportedVariableType", "Unsupported variable type in a cell");
			}
		}
		return list;
	} else {
		if (imagData == NULL) {
#define CASE(cls,c_type,d_type,py_ctor) case cls: for (int i = 0; i < nelem; i++) { \
dtype=d_type; \
PyObject *item = py_ctor(*((c_type*) data)); \
if (debug) mexPrintf("Setting %d to %f (item = 0x%08X)\n", i, (double) *((c_type*) data), item); \
data += sizeof(c_type); \
if (PyList_SetItem(list, i, item) == -1) PyErr_Print(); } \
break
			switch(cls) {
			CASE(mxLOGICAL_CLASS, bool, "bool", PyBool_FromLong);
			CASE(mxDOUBLE_CLASS, double, "float64", PyFloat_FromDouble);
			CASE(mxSINGLE_CLASS, float, "float32", PyFloat_FromDouble);
			CASE(mxINT8_CLASS, char, "int8", PyInt_FromLong);
			CASE(mxUINT8_CLASS, unsigned char, "uint8", PyInt_FromLong);
			CASE(mxINT16_CLASS, short, "int16", PyInt_FromLong);
			CASE(mxUINT16_CLASS, unsigned short, "uint16", PyInt_FromLong);
			CASE(mxINT32_CLASS, int, "int32", PyInt_FromLong);
			CASE(mxUINT32_CLASS, unsigned int, "uint32", PyLong_FromLongLong);
			CASE(mxINT64_CLASS, long long, "int64", PyLong_FromLongLong);
			CASE(mxUINT64_CLASS, unsigned long long, "uint64", PyLong_FromUnsignedLongLong);
			default:
				mexErrMsgIdAndTxt("matpy:UnsupportedVariableType", "Unsupported variable type");
			}
		} else {
#undef CASE
#define CASE(cls,c_type,d_type) case cls: for (int i = 0; i < nelem; i++) { \
dtype = d_type; \
PyObject *item = PyComplex_FromDoubles(*((c_type*) data), *((c_type*) imagData)); \
data += sizeof(c_type); \
imagData += sizeof(c_type); \
PyList_SetItem(list, i, item); } \
break
			switch(cls) {
			CASE(mxDOUBLE_CLASS, double, "complex128");
			CASE(mxSINGLE_CLASS, float, "complex64");
			CASE(mxINT8_CLASS, char, "complex64");
			CASE(mxUINT8_CLASS, unsigned char, "complex64");
			CASE(mxINT16_CLASS, short, "complex64");
			CASE(mxUINT16_CLASS, unsigned short, "complex64");
			CASE(mxINT32_CLASS, int, "complex128");
			CASE(mxUINT32_CLASS, unsigned int, "complex128");
			CASE(mxINT64_CLASS, long long, "complex128");
			CASE(mxUINT64_CLASS, unsigned long long, "complex128");
			default:
				mexErrMsgIdAndTxt("matpy:UnsupportedVariableType", "Unsupported variable type");
			}
		}
	}

	PyObject *ret = NULL;
	if(NULL != dtype)
	{
		PyObject *shape = PyList_New(ndims);
		for (size_t i = 0; i < ndims; i++)
		{
			PyList_SetItem(shape, i, PyInt_FromLong(dims[i]));
		}

		PyObject *args = PyTuple_New(1);
		PyTuple_SetItem(args, 0, list);
		PyObject *kwargs = PyDict_New();
		PyObject *dtypePyStr = PyString_FromString(dtype);
		PyDict_SetItemString(kwargs, "dtype", dtypePyStr);
		Py_DECREF(dtypePyStr);

		if (debug) mexPrintf("list = 0x%08X dtype = %s\n", list, dtype);
		PyObject *ndary = PyObject_Call(np_array_fun, args, kwargs);
		Py_DECREF(kwargs);
		Py_DECREF(args);
		if (ndary == NULL) 
		{
			PyErr_Print();
			mexErrMsgIdAndTxt("matpy:PythonError", "Error converting Python value");
		}

		if (debug) mexPrintf("ndary = 0x%08X\n", ndary);
		PyObject *reshape_meth = PyObject_GetAttrString(ndary, "reshape");
		args = PyTuple_New(1);
		PyTuple_SetItem(args, 0, shape);
		kwargs = PyDict_New();
		PyDict_SetItemString(kwargs, "order", PyString_FromString("F"));
	
		ret = PyObject_Call(reshape_meth, args, kwargs);
		Py_DECREF(args);
		Py_DECREF(kwargs);
		Py_DECREF(reshape_meth);
		Py_DECREF(ndary);
	}
	else 
	{
		mexErrMsgIdAndTxt("matpy:UnsupportedVariableType", "Unsupported variable type");
	}

	return ret;
}

static mxArray* py2mat(PyObject *o) {
#undef CASE
#define CASE(check, c_type, cls, conv) \
	if (check(o)) { \
		if (debug) mexPrintf("%s\n", #c_type); \
		mwSize dims[] = {1,1}; \
		mxArray *a = mxCreateNumericArray(2, dims, cls, mxREAL); \
		c_type *data = (c_type*) mxGetData(a); \
		*data = (c_type) conv(o); \
		Py_DECREF(o); \
		return a; \
	}
	CASE(PyBool_Check, bool, mxLOGICAL_CLASS, PyInt_AsLong) else
	CASE(PyInt_Check, int, mxINT32_CLASS, PyInt_AsLong) else
	CASE(PyLong_Check, long long, mxINT64_CLASS, PyLong_AsLongLong) else
	CASE(PyFloat_Check, double, mxDOUBLE_CLASS, PyFloat_AsDouble) else
	if (PyComplex_Check(o)) {
		mwSize dims[] = {1,1};
		mxArray *a = mxCreateNumericArray(2, dims, mxDOUBLE_CLASS, mxCOMPLEX);
		double *data = mxGetPr(a);
		double *imagData = mxGetPi(a);
		*data = PyComplex_RealAsDouble(o);
		*imagData = PyComplex_ImagAsDouble(o);
		Py_DECREF(o);
		return a;
	} else if (PyUnicode_Check(o)) {
		PyObject *tmpASCII = PyUnicode_AsASCIIString(o);
		PyObject *tmpObject = PyObject_Str(tmpASCII);
		Py_DECREF(tmpASCII);
		char* tmpString = PyString_AsString(tmpObject);
		Py_DECREF(tmpObject);
		mxArray *a = mxCreateString(tmpString);
		Py_DECREF(o);
		return a;
	} else if (PyString_Check(o)) {
		mxArray *a = mxCreateString(PyString_AsString(o));
		Py_DECREF(o);
		return a;
	} else if (PyObject_IsInstance(o, ndarray_cls)) {
		PyObject *shape = PyObject_GetAttrString(o, "shape");
		PyObject *dtype = PyObject_GetAttrString(o, "dtype");
		PyObject *dtype_as_str = PyObject_Str(dtype);
		Py_DECREF(dtype);
		char *dtype_str = PyString_AsString(dtype_as_str);
		mwSize ndims = PySequence_Size(shape);
		mwSize *dims = new mwSize[ndims];
		mwSize nelem = 1;
		for (int i = 0; i < ndims; i++) {
			PyObject *item = PySequence_GetItem(shape, i);
			dims[i] = PyInt_AsLong(item);
			Py_DECREF(item);
			nelem *= dims[i];
		}
		Py_DECREF(shape);
		if (debug) mexPrintf("ndims = %d, dims[0] = %d, dims[1] = %d, dtype = %s\n", ndims, dims[0], ndims > 1 ? dims[1] : 1, dtype_str);

		PyObject *lin_shape = PyList_New(1);
		PyList_SetItem(lin_shape, 0, PyInt_FromLong(nelem));
		PyObject *reshape_meth = PyObject_GetAttrString(o, "reshape");
		PyObject *args = PyTuple_New(1);
		PyObject *kwargs = PyDict_New();
		PyTuple_SetItem(args, 0, lin_shape);
		PyDict_SetItemString(kwargs, "order", PyString_FromString("F"));
		PyObject *reshaped = PyObject_Call(reshape_meth, args, kwargs);
		Py_DECREF(args);
		Py_DECREF(kwargs);
		Py_DECREF(reshape_meth);
		// PyObject *reshaped = PyObject_CallMethod(o, "reshape", "O", lin_shape);
		Py_DECREF(o);
		PyObject *list = PyObject_CallMethod(reshaped, (char *)"tolist", NULL);
		Py_DECREF(reshaped);

		mxArray *a = NULL;
#undef CASE
#define CASE(str,c_type,cls,ctor) \
	if (!strcmp(dtype_str,str)) { \
		a = mxCreateNumericArray(ndims, dims, cls, mxREAL); \
		c_type *data = (c_type*) mxGetData(a); \
		for (size_t i = 0; i < nelem; i++) { \
			PyObject *item = PySequence_GetItem(list, i); \
			*data++ = (c_type) ctor(item); \
			Py_DECREF(item); \
		} \
	}
		CASE("bool", bool, mxLOGICAL_CLASS, PyInt_AsLong) else
		CASE("float32", float, mxSINGLE_CLASS, PyFloat_AsDouble) else
		CASE("float64", double, mxDOUBLE_CLASS, PyFloat_AsDouble)
		CASE("int8", char, mxINT8_CLASS, PyInt_AsLong) else
		CASE("uint8", unsigned char, mxUINT8_CLASS, PyInt_AsLong) else
		CASE("int16", short, mxINT16_CLASS, PyInt_AsLong) else
		CASE("uint16", unsigned short, mxUINT16_CLASS, PyInt_AsLong) else
		CASE("int32", int, mxINT32_CLASS, PyInt_AsLong) else
		CASE("uint32", unsigned int, mxUINT32_CLASS, PyInt_AsUnsignedLongMask) else
		CASE("int64", long long, mxINT64_CLASS, PyLong_AsLongLong) else
		CASE("uint64", unsigned long long, mxUINT64_CLASS, PyLong_AsUnsignedLongLong) else

#undef CASE
#define CASE(str,c_type,cls) \
	if (!strcmp(dtype_str,str)) { \
		a = mxCreateNumericArray(ndims, dims, cls, mxCOMPLEX); \
		c_type *data = (c_type*) mxGetData(a); \
		c_type *imagData = (c_type*) mxGetImagData(a); \
		for (int i = 0; i < nelem; i++) { \
			PyObject *item = PySequence_GetItem(list, i); \
			*data++ = (c_type) PyComplex_RealAsDouble(item); \
			*imagData++ = (c_type) PyComplex_ImagAsDouble(item); \
			Py_DECREF(item); \
		} \
	}
		CASE("complex64", float, mxSINGLE_CLASS) else
		CASE("complex128", double, mxDOUBLE_CLASS);

		Py_DECREF(list);
		Py_DECREF(dtype_as_str);

		if (a == NULL) {
			mexErrMsgIdAndTxt("matpy:UnsupportedVariableType", "Unsupported variable type");
		}

		return a;
	} else if (PySequence_Check(o)) {
		mwSize nelem = PySequence_Size(o);
		mwSize dims[] = {1, nelem};
		mxArray *a = mxCreateCellArray(2, dims);
		if (debug) mexPrintf("a = 0x%08X nelem = %d\n", a, nelem);
		for (int i = 0; i < nelem; i++) {
			PyObject *item = PySequence_GetItem(o, i);
			mxArray *mat_item = py2mat(item);
			if(mat_item == NULL)
			{
				Py_DECREF(o);
				mexErrMsgIdAndTxt("matpy:ConversionError", "Error converting to MATLAB variable");
			}
			if (debug) mexPrintf("mat_item = 0x%08X\n", mat_item);
			mxSetCell(a, i, mat_item);
		}
		Py_DECREF(o);
		return a;
	} else if(PyDict_Check(o)){
		PyObject *keys = PyDict_Keys(o);
		PyObject *items = PyDict_Values(o);
		mwSize nfields = PyDict_Size(o);
		mwSize nelem;
		char **fieldNames = (char **)mxMalloc(nfields * sizeof(char *));
		int i, j;

		for(i = 0; i < nfields; i++) {
            if(!PyList_Check(PyList_GetItem(items, 0))) {
                Py_DECREF(o);
                Py_DECREF(keys);
                Py_DECREF(items);
                mexErrMsgIdAndTxt("matpy:IncorrectStructForm" ,"Dictionary must have a list of values for each field");
            }

            if(i == 0)
                nelem = PyList_Size(PyList_GetItem(items, 0));

			if(nelem != PyList_Size(PyList_GetItem(items, i))) {
				Py_DECREF(o);
				Py_DECREF(keys);
				Py_DECREF(items);
				mexErrMsgIdAndTxt("matpy:IncorrectStructForm" ,"Inconsistent number of elements");
			}
		}

		if (debug) mexPrintf("nfields = %d, nelem = %d\n", nfields, nelem);

		for(i = 0; i < nfields; i++) {
			PyObject *field = PyList_GetItem(keys, i);
			fieldNames[i] = PyString_AsString(field);
		}

		mwSize dims[] = {1, nelem};
		mxArray *a = mxCreateStructArray(2, dims, nfields, (const char**)fieldNames);

		for(i = 0; i < nfields; i++) {
			PyObject *item = PySequence_GetItem(items, i);
			for(j = 0; j < nelem; j++) {
				mxArray *mat_item = py2mat(PySequence_GetItem(item, j));
				if(mat_item == NULL)
				{
					Py_DECREF(o);
					mexErrMsgIdAndTxt("matpy:ConversionError", "Error converting to MATLAB variable");
				}
				if (debug) mexPrintf("mat_item = 0x%08X\n", mat_item);
				mxSetFieldByNumber(a, j, i, mat_item);
			}
		}

		Py_DECREF(o);
		Py_DECREF(keys);
		Py_DECREF(items);
		return a;
	} else{
		Py_DECREF(o);
		mexErrMsgIdAndTxt("matpy:UnsupportedVariableType", "Unsupported variable type");
	}
	return NULL;
}

static void do_get() 
{
    if(nrhs != 2) 
    {
        mexErrMsgIdAndTxt("matpy:WrongNumberOfInputs", "Usage: var = py('get', expr)");
    }
    if(!mxIsChar(prhs[1])) 
    {
        mexErrMsgIdAndTxt("matpy:WrongInputVariableType", "Usage: var = py('get', expr)");
    }
    if(nlhs != 1) 
    {
        mexErrMsgIdAndTxt("matpy:NoOutputsVariable", "Usage: var = py('get', expr)");
    }

	char *expr = mxArrayToString(prhs[1]);
	if (debug) mexPrintf("Evaluating: %s\n", expr);
	PyCodeObject *code = (PyCodeObject*) Py_CompileString(expr, "no_source", Py_eval_input);
	mxFree(expr);
	if (code == NULL) 
	{
		PyErr_Print();
		mexErrMsgIdAndTxt("matpy:PythonError", "Error compiling expression");
	}

	PyObject *o = PyEval_EvalCode(code, globals, globals);
	if (o == NULL) 
	{
		PyErr_Print();
		mexErrMsgIdAndTxt("matpy:PythonError", "Error evaluating Python expression");
	}
	Py_DECREF(code);

	plhs[0] = py2mat(o);
	if (plhs[0] == NULL) {
		mexErrMsgIdAndTxt("matpy:ConversionError", "Error converting to MATLAB variable");
	}
}

static void do_set() 
{
    
    if(nrhs != 3) 
    {
        mexErrMsgIdAndTxt("matpy:WrongNumberOfInputs", "Usage: py('set', var_name, var)");
    }
    if(!mxIsChar(prhs[1])) 
    {
        mexErrMsgIdAndTxt("matpy:WrongInputVariableType", "Usage: py('set', var_name, var)");
    }

	char *var_name = mxArrayToString(prhs[1]);
	PyObject *var = mat2py(prhs[2]);

	if(NULL != var)
	{
		addVariableToPython(var_name, var);
		mxFree(var_name);
	}
	else 
	{
		if(NULL != var_name)
		{
			mxFree(var_name);
		}
		mexErrMsgIdAndTxt("matpy:ExportError", "Error while export to Python");
	}

}

static void do_eval() 
{
    
    if(nrhs != 2) 
    {
        mexErrMsgIdAndTxt("matpy:WrongNumberOfInputs", "Usage: py('eval', stmt)");
    }
    if(!mxIsChar(prhs[1])) 
    {
        mexErrMsgIdAndTxt("matpy:WrongInputVariableType", "Usage: py('eval', stmt)");
    }

	char *stmt = mxArrayToString(prhs[1]);

	if (debug) mexPrintf("Evaluating: %s\n", stmt);
	// PyObject *locals = PyDict_New();
	PyObject *o = PyRun_String(stmt, Py_file_input, globals, globals);
	// Py_DECREF(locals);
	mxFree(stmt);
	if (o == NULL) 
	{
		PyErr_Print();
		mexErrMsgIdAndTxt("matpy:PythonError", "Error while evaluating Python statement");
	}

	if (nlhs > 0)
	{
		plhs[0] = py2mat(o);
		if (plhs[0] == NULL) {
			mexErrMsgIdAndTxt("matpy:ConversionError", "Error converting to MATLAB variable");
		}
	}
}

void mexFunction(int nlhs_, mxArray *plhs_[], int nrhs_, const mxArray *prhs_[]) {
	nlhs = nlhs_;
	plhs = plhs_;
	nrhs = nrhs_;
	prhs = prhs_;
	static bool been_here = false;

	if (!been_here) {
		if (debug) mexPrintf("Initializing...\n");
		dlopen("libpython2.7.so", RTLD_LAZY |RTLD_GLOBAL);
		Py_SetProgramName((char*)PYPATH);
		Py_Initialize();
		initMatpyPrint();
        module = PyImport_AddModule("__main__");
        if (NULL == module) 
        {
            PyErr_Print();
            mexErrMsgIdAndTxt("matpy:NumpyNotAccessible", "numpy not accessible");
        }
		globals = PyModule_GetDict(module);
		PyObject *numpy = PyImport_ImportModule("numpy");
		if (numpy == NULL) {
			PyErr_Print();
			mexErrMsgIdAndTxt("matpy:NumpyNotAccessible", "numpy not accessible");
		}
		PyObject *numpy_dict = PyModule_GetDict(numpy);
		Py_DECREF(numpy);
		if (debug) mexPrintf("numpy_dict = 0x%08X\n", numpy_dict);
		np_array_fun = PyDict_GetItemString(numpy_dict, "array");
		if (np_array_fun == 0) {
			PyErr_Print();
			mexErrMsgIdAndTxt("matpy:NumpyArrayNotAccessible", "numpy.array not accessible");
		}
		if (debug) mexPrintf("np_array_fun = 0x%08X\n", np_array_fun);
		ndarray_cls = PyDict_GetItemString(numpy_dict, "ndarray");
		been_here = true;
	}

    if(nrhs == 0) 
    {
        mexErrMsgIdAndTxt("matpy:WrongNumberOfInputs", "Usage: py(cmd, varargin)");
    }

    if(!mxIsChar(prhs[0])) 
    {
        mexErrMsgIdAndTxt("matpy:WrongInputVariableType", "Usage: py(cmd, varargin)");
    }

	char *cmd = mxArrayToString(prhs[0]);
	if (!strcmp(cmd, "eval")) {
		mxFree(cmd);
		do_eval();
		return;
	} else if (!strcmp(cmd, "set")) {
		mxFree(cmd);
		do_set();
		return;
	} else if (!strcmp(cmd, "get")) {
		mxFree(cmd);
		do_get();
		return;
	} else if (!strcmp(cmd, "debugon")) {
		debug = true;
	} else if (!strcmp(cmd, "debugoff")) {
		debug = false;
	} else {
		mxFree(cmd);
		mexErrMsgIdAndTxt("matpy:UnrecognizedCommand", "Unrecognized cmd");
	}
	mxFree(cmd);
}

// Note: This function steals the reference to value.
static void addVariableToPython(const char* name, PyObject *value)
{
	int success = PyModule_AddObject(module, name, value);

	if(-1 == success)
	{
		const size_t MAX_SIZE = 100;
		char message[MAX_SIZE];
		snprintf(message, MAX_SIZE, "Failed to add '%s' to the module\nValue is: %s", name, pyObjectToString(value));
		mexErrMsgIdAndTxt("matpy:FailedToAddVariableToPython", message);
	}
}

static const char *pyObjectToString(PyObject *pyObject)
{
	PyObject* temp = PyObject_Repr(pyObject);
	const char *result = PyString_AsString(temp);
	return result;
}


