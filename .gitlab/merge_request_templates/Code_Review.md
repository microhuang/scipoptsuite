### Description

[Add a description of the introduced changes here.]

### Code review

* [ ] The code change is correct.
* [ ] The naming and place of new methods is clear and consistent *or* no new methods have been added.
* [ ] Emphasis settings are up to date.
* [ ] The code is sufficiently documented.
* [ ] The coding style is OK, see https://scipopt.org/doc/html/CODE.php.

### Documentation and building

* [ ] The CHANGELOG is up to date (including API changes if present in this merge request.)
* [ ] The user documentation is up to date (doc/xternal.c, doc/inc/faq/, installation instructions, ...).
* [ ] makedist.sh and the dependencies are up to date (for make only).

### Testing

* [ ] ctest passes without errors: `jenkins ctest {soplex master,soplex bugfix,cplex,gurobi,mosek,xpress}`.
* [ ] The performance impact has been checked on mi(nl)pdev-solvable *or* the changed code will not be executed by default.
* [ ] The new code is sufficiently covered by tests (perhaps, new coverage settings or new unit tests have been added).

### Does this merge request introduce an API change? :warning:

* [ ] As far as possible, the code ensures backwards compatibility.
* [ ] After merging, the `SCIP_APIVERSION` is updated if necessary (use label `public SCIP API`; in particular if new `SCIP_EXPORT` methods have been added).
* [ ] No parameter was added/deleted/changed *or* the MR is tagged with the label 'default parameter'. (This includes the addition of parameters by adding/deleting a plugin.)
