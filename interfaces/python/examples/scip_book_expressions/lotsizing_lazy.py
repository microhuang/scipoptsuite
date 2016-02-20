"""
lotsizing_cut.py:  solve the single-item lot-sizing problem.

Approaches:
    - sils: solve the problem using the standard formulation
    - sils_cut: solve the problem using cutting planes

Copyright (c) by Joao Pedro PEDROSO and Mikio KUBO, 2012
"""
from pyscipopt import Model, quicksum, multidict

def sils(T,f,c,d,h):
    """sils -- LP lotsizing for the single item lot sizing problem
    Parameters:
        - T: number of periods
        - P: set of products
        - f[t]: set-up costs (on period t)
        - c[t]: variable costs
        - d[t]: demand values
        - h[t]: holding costs
    Returns a model, ready to be solved.
    """
    model = Model("single item lotsizing")
    Ts = range(1,T+1)
    M = sum(d[t] for t in Ts)
    y,x,I = {},{},{}
    for t in Ts:
        y[t] = model.addVar(vtype="I", ub=1, name="y(%s)"%t)
        x[t] = model.addVar(vtype="C", ub=M, name="x(%s)"%t)
        I[t] = model.addVar(vtype="C", name="I(%s)"%t)
    I[0] = 0

    for t in Ts:
        model.addCons(x[t] <= M*y[t], "ConstrUB(%s)"%t)
        model.addCons(I[t-1] + x[t] == I[t] + d[t], "FlowCons(%s)"%t)

    model.setObjective(\
        quicksum(f[t]*y[t] + c[t]*x[t] + h[t]*I[t] for t in Ts),\
        "minimize")

    model.data = y,x,I
    return model


def sils_cut(T,f,c,d,h):
    """solve_sils -- solve the lot sizing problem with cutting planes
       - start with a relaxed model
       - used lazy constraints to elimitate fractional setup variables with cutting planes
    Parameters:
        - T: number of periods
        - P: set of products
        - f[t]: set-up costs (on period t)
        - c[t]: variable costs
        - d[t]: demand values
        - h[t]: holding costs
    Returns the final model solved, with all necessary cuts added.
    """
    Ts = range(1,T+1)

    def sils_callback(model,where):
        # remember to set     model.params.DualReductions = 0     before using!
        if where != GRB.Callback.MIPSOL and where != GRB.Callback.MIPNODE:
            return

        if where == GRB.Callback.MIPSOL:
            getsol = model.cbGetSolution
            cut = model.cbLazy
        elif where == GRB.Callback.MIPNODE:
            getsol = model.cbGetNodeRel
            cut = model.cbCut
        else:
            return

        for ell in Ts:
            lhs = 0
            S,L = [],[]
            for t in range(1,ell+1):
                yt = model.getsol(y[t])
                xt = model.getsol(x[t])
                if D[t,ell]*yt < xt:
                    S.append(t)
                    lhs += D[t,ell]*yt
                else:
                    L.append(t)
                    lhs += xt
            if lhs < D[1,ell]:
                # add cutting plane constraint
                cut(quicksum([x[t] for t in L]) +\
                    quicksum(D[t,ell] * y[t] for t in S)
                    >= D[1,ell])
        return


    model = sils(T,f,c,d,h)
    y,x,I = model.data

    # relax integer variables
    for t in Ts:
        y[t].vtype = "C"
    model.addVar(vtype="B", name="fake")        # for making the problem MIP

    # compute D[i,j] = sum_{t=i}^j d[t]
    D = {}
    for t in Ts:
        s = 0
        for j in range(t,T+1):
            s += d[j]
            D[t,j] = s

    model.data = y,x,I
    return model,sils_callback


def mk_example():
    """mk_example: book example for the single item lot sizing"""
    T = 5
    _,f,c,d,h = multidict({
        1 :  [3,1,5,1],
        2 :  [3,1,7,1],
        3 :  [3,3,3,1],
        4 :  [3,3,6,1],
        5 :  [3,3,4,1],
        })

    return T,f,c,d,h


if __name__ == "__main__":
    T,f,c,d,h = mk_example()

    model = sils(T,f,c,d,h)
    y,x,I = model.data
    model.optimize()
    print("\nOptimal value [standard]:",model.getObjVal())
    print("%8s%8s%8s%8s%8s%8s%12s%12s" % ("t","fix","var","h","dem","y","x","I"))
    for t in range(1,T+1):
        print("%8s%8s%8s%8s%8s%8s%12s%12s" % (t,f[t],c[t],h[t],d[t],model.getVal(y[t]),model.getVal(x[t]),model.getVal(I[t])))

    model,sils_callback = sils_cut(T,f,c,d,h)
    model.params.DualReductions = 0
    model.optimize(sils_callback)
    y,x,I = model.data
    print("\nOptimal value [cutting planes]:",model.getObjVal())
    print("%8s%8s%8s%8s%8s%8s%12s%12s" % ("t","fix","var","h","dem","y","x","I"))
    for t in range(1,T+1):
        print("%8s%8s%8s%8s%8s%8s%12s%12s" % (t,f[t],c[t],h[t],d[t],model.getVal(y[t]),model.getVal(x[t]),model.getVal(I[t])))
