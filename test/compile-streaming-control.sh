# Compile the configure parser and its module dependencies in the runner's
# temporary working directory. The parser uses the Q65 caller limit at compile time.
"$FC" -c "$REPO_ROOT/lib/types.f90"
"$FC" -c -cpp -I"$REPO_ROOT/lib/qra/q65" "$REPO_ROOT/lib/qra/q65/q65_callers.F90"
"$FC" -c "$SRC"
