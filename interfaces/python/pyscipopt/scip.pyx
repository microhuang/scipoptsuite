# Copyright (C) 2012-2013 Robert Schwarz
#   see file 'LICENSE' for details.

cimport pyscipopt.scip as scip


def scipErrorHandler(function):
    def wrapper(*args, **kwargs):
        return PY_SCIP_CALL(function(*args, **kwargs))
    return wrapper

# Mapping the SCIP_RESULT enum to a python class
# This is required to return SCIP_RESULT in the python code
cdef class scip_result:
    didnotrun   =   1
    delayed     =   2
    didnotfind  =   3
    feasible    =   4
    infeasible  =   5
    unbounded   =   6
    cutoff      =   7
    separated   =   8
    newround    =   9
    reducedom   =  10
    consadded   =  11
    consshanged =  12
    branched    =  13
    solvelp     =  14
    foundsol    =  15
    suspended   =  16
    success     =  17


cdef class scip_paramsetting:
    default     = 0
    agressive   = 1
    fast        = 2
    off         = 3


def PY_SCIP_CALL(scip.SCIP_RETCODE rc):
    if rc == scip.SCIP_OKAY:
        pass
    elif rc == scip.SCIP_ERROR:
        raise StandardError('SCIP: unspecified error!')
    elif rc == scip.SCIP_NOMEMORY:
        raise MemoryError('SCIP: insufficient memory error!')
    elif rc == scip.SCIP_READERROR:
        raise IOError('SCIP: read error!')
    elif rc == scip.SCIP_WRITEERROR:
        raise IOError('SCIP: write error!')
    elif rc == scip.SCIP_NOFILE:
        raise IOError('SCIP: file not found error!')
    elif rc == scip.SCIP_FILECREATEERROR:
        raise IOError('SCIP: cannot create file!')
    elif rc == scip.SCIP_LPERROR:
        raise StandardError('SCIP: error in LP solver!')
    elif rc == scip.SCIP_NOPROBLEM:
        raise StandardError('SCIP: no problem exists!')
    elif rc == scip.SCIP_INVALIDCALL:
        raise StandardError('SCIP: method cannot be called at this time'
                            + ' in solution process!')
    elif rc == scip.SCIP_INVALIDDATA:
        raise StandardError('SCIP: error in input data!')
    elif rc == scip.SCIP_INVALIDRESULT:
        raise StandardError('SCIP: method returned an invalid result code!')
    elif rc == scip.SCIP_PLUGINNOTFOUND:
        raise StandardError('SCIP: a required plugin was not found !')
    elif rc == scip.SCIP_PARAMETERUNKNOWN:
        raise KeyError('SCIP: the parameter with the given name was not found!')
    elif rc == scip.SCIP_PARAMETERWRONGTYPE:
        raise LookupError('SCIP: the parameter is not of the expected type!')
    elif rc == scip.SCIP_PARAMETERWRONGVAL:
        raise ValueError('SCIP: the value is invalid for the given parameter!')
    elif rc == scip.SCIP_KEYALREADYEXISTING:
        raise KeyError('SCIP: the given key is already existing in table!')
    elif rc == scip.SCIP_MAXDEPTHLEVEL:
        raise StandardError('SCIP: maximal branching depth level exceeded!')
    else:
        raise StandardError('SCIP: unknown return code!')
    return rc

cdef class Solution:
    cdef scip.SCIP_SOL* _solution

cdef class Var:
    cdef scip.SCIP_VAR* _var

cdef class Cons:
    cdef scip.SCIP_CONS* _cons


cdef class Solver:
    cdef scip.SCIP* _scip

    @scipErrorHandler
    def create(self):
        return scip.SCIPcreate(&self._scip)

    @scipErrorHandler
    def includeDefaultPlugins(self):
        return scip.SCIPincludeDefaultPlugins(self._scip)

    @scipErrorHandler
    def createProbBasic(self, problemName):
        return scip.SCIPcreateProbBasic(self._scip, problemName)

    @scipErrorHandler
    def free(self):
        return scip.SCIPfree(&self._scip)

    @scipErrorHandler
    def freeTransform(self):
        return scip.SCIPfreeTransform(self._scip)

    #@scipErrorHandler       We'll be able to use decorators when we
    #                        interface the relevant classes (SCIP_VAR, ...)
    cdef _createVarBasic(self, scip.SCIP_VAR** scip_var, name,
                        lb, ub, obj, scip.SCIP_VARTYPE varType):
        PY_SCIP_CALL(SCIPcreateVarBasic(self._scip, scip_var,
                           name, lb, ub, obj, varType))

    cdef _addVar(self, scip.SCIP_VAR* scip_var):
        PY_SCIP_CALL(SCIPaddVar(self._scip, scip_var))

    cdef _createConsLinear(self, scip.SCIP_CONS** cons, name, nvars,
                                SCIP_VAR** vars, SCIP_Real* vals, lhs, rhs,
                                initial=True, separate=True, enforce=True, check=True,
                                propagate=True, local=False, modifiable=False, dynamic=False,
                                removable=False, stickingatnode=False):
        PY_SCIP_CALL(scip.SCIPcreateConsLinear(self._scip, cons,
                                                    name, nvars, vars, vals,
                                                    lhs, rhs, initial, separate, enforce,
                                                    check, propagate, local, modifiable,
                                                    dynamic, removable, stickingatnode) )

    cdef _addCoefLinear(self, scip.SCIP_CONS* cons, SCIP_VAR* var, val):
        PY_SCIP_CALL(scip.SCIPaddCoefLinear(self._scip, cons, var, val))

    cdef _addCons(self, scip.SCIP_CONS* cons):
        PY_SCIP_CALL(scip.SCIPaddCons(self._scip, cons))

    cdef _writeVarName(self, scip.SCIP_VAR* var):
        PY_SCIP_CALL(scip.SCIPwriteVarName(self._scip, NULL, var, False))

    cdef _releaseVar(self, scip.SCIP_VAR* var):
        PY_SCIP_CALL(scip.SCIPreleaseVar(self._scip, &var))
        

    # Setting the objective sense
    def setMinimise(self):
        PY_SCIP_CALL(scip.SCIPsetObjsense(self._scip, SCIP_OBJSENSE_MINIMIZE))

    def setMaximise(self):
        PY_SCIP_CALL(scip.SCIPsetObjsense(self._scip, SCIP_OBJSENSE_MAXIMIZE))

    # Setting parameters
    def setPresolve(self, setting):
        PY_SCIP_CALL(scip.SCIPsetPresolving(self._scip, setting, True))


    # Variable Functions
    # Creating and adding a continuous variable
    def addContVar(self, name, lb=0.0, ub=None, obj=0.0):
        if ub is None:
            ub = scip.SCIPinfinity(self._scip)
        cdef scip.SCIP_VAR* scip_var
        self._createVarBasic(&scip_var, name, lb, ub, obj,
                            scip.SCIP_VARTYPE_CONTINUOUS)

        self._addVar(scip_var)
        var = Var()
        var._var = scip_var

        self._releaseVar(scip_var)
        return var

    # Creating and adding an integer variable
        # Note: setting the bounds to 0.0 and 1.0 will automatically
        # convert this variable to binary.
    def addIntVar(self, name, lb=0.0, ub=None, obj=0.0):
        if ub is None:
            ub = scip.SCIPinfinity(self._scip)
        cdef scip.SCIP_VAR* scip_var
        self._createVarBasic(&scip_var, name, lb, ub, obj,
                            scip.SCIP_VARTYPE_INTEGER)

        self._addVar(scip_var)

        var = Var()
        var._var = scip_var

        self._releaseVar(scip_var)
        return var

    # Release the variable
    def releaseVar(self, Var var):
        cdef scip.SCIP_VAR* _var
        _var = <scip.SCIP_VAR*>var._var
        self._releaseVar(_var)

    # Retrieving the pointer for the transformed variable
    def getTransformedVar(self, Var var):
        transvar = Var()
        PY_SCIP_CALL(scip.SCIPtransformVar(self._scip, var._var, &transvar._var))
        return transvar

    # Constraint functions
    # Adding a linear constraint. By default the lhs is set to 0.0.
    # If the lhs is to be unbounded, then you set lhs to None.
    # By default the rhs is unbounded.
    def addCons(self, coeffs, lhs=0.0, rhs=None,
                initial=True, separate=True, enforce=True, check=True,
                propagate=True, local=False, modifiable=False, dynamic=False,
                removable=False, stickingatnode=False):
        if lhs is None:
            lhs = -scip.SCIPinfinity(self._scip)
        if rhs is None:
            rhs = scip.SCIPinfinity(self._scip)
        cdef scip.SCIP_CONS* scip_cons
        self._createConsLinear(&scip_cons, "cons", 0, NULL, NULL, lhs, rhs,
                                initial, separate, enforce, check, propagate,
                                local, modifiable, dynamic, removable, stickingatnode)
        cdef Var var
        cdef scip.SCIP_Real coeff
        for k in coeffs:
            var = <Var>k
            coeff = <scip.SCIP_Real>coeffs[k]
            self._addCoefLinear(scip_cons, var._var, coeff)
        self._addCons(scip_cons)
        cons = Cons()
        cons._cons = scip_cons
        return cons


    def addConsCoeff(self, Cons cons, Var var, coeff):
        cdef scip.SCIP_CONS* _cons
        _cons = <scip.SCIP_CONS*>cons._cons
        cdef scip.SCIP_VAR* _var
        _var = <scip.SCIP_VAR*>var._var
        PY_SCIP_CALL(scip.SCIPaddCoefLinear(self._scip, _cons, _var, coeff))


    # Retrieving the pointer for the transformed constraint
    def getTransformedCons(self, Cons cons):
        transcons = Cons()
        PY_SCIP_CALL(scip.SCIPtransformCons(self._scip, cons._cons, &transcons._cons))
        return transcons

    # Retrieving the dual solution for a linear constraint
    def getDualsolLinear(self, Cons cons):
        return scip.SCIPgetDualsolLinear(self._scip, cons._cons)

    # Retrieving the dual farkas value for a linear constraint
    def getDualfarkasLinear(self, Cons cons):
        return scip.SCIPgetDualfarkasLinear(self._scip, cons._cons)


    # Problem solving functions
    def solve(self):
        PY_SCIP_CALL( scip.SCIPsolve(self._scip) )



    # Solution functions
    # Retrieve the current best solution
    def getBestSol(self):
        solution = Solution()
        solution._solution = scip.SCIPgetBestSol(self._scip)
        return solution

    # Get problem objective value
    def getSolObjVal(self, Solution solution, transform=True):
        cdef scip.SCIP_SOL* _solution
        _solution = <scip.SCIP_SOL*>solution._solution
        if transform:
            objval = scip.SCIPgetSolTransObj(self._scip, _solution)
        else:
            objval = scip.SCIPgetSolOrigObj(self._scip, _solution)
        return objval


    # Retrieve the value of the variable in the final solution
    def getVal(self, Solution solution, Var var):
        cdef scip.SCIP_SOL* _solution
        _solution = <scip.SCIP_SOL*>solution._solution
        cdef scip.SCIP_VAR* _var
        _var = <scip.SCIP_VAR*>var._var
        return scip.SCIPgetSolVal(self._scip, _solution, _var)

    # Write the names of the variable to the std out.
    def writeName(self, Var var):
        cdef scip.SCIP_VAR* _var
        _var = <scip.SCIP_VAR*>var._var
        self._writeVarName(_var)


    # Statistic Methods
    def printStatistics(self):
        PY_SCIP_CALL(scip.SCIPprintStatistics(self._scip, NULL))


    # Parameter Methods
    def setBoolParam(self, name, value):
        PY_SCIP_CALL(scip.SCIPsetBoolParam(self._scip, name, value))

    def setIntParam(self, name, value):
        PY_SCIP_CALL(scip.SCIPsetIntParam(self._scip, name, value))

    def setLongintParam(self, name, value):
        PY_SCIP_CALL(scip.SCIPsetLongintParam(self._scip, name, value))

    def setRealParam(self, name, value):
        PY_SCIP_CALL(scip.SCIPsetRealParam(self._scip, name, value))

    def setCharParam(self, name, value):
        PY_SCIP_CALL(scip.SCIPsetCharParam(self._scip, name, value))

    def setStringParam(self, name, value):
        PY_SCIP_CALL(scip.SCIPsetStringParam(self._scip, name, value))
