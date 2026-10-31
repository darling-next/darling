extern double pow(double b, double p);
extern float powf(float b, float p);

/*
 * Darwin's optimizer folds pow(10, x) back into __exp10. These entry points
 * must call the scalar backend instead of recursively calling themselves.
 * The sibling fork disables builtins for this file; keep the restriction
 * local to each backend call, as in sincos.c.
 */
__attribute__((no_builtin("pow")))
double __exp10(double x) {
	return pow(10, x);
};

__attribute__((no_builtin("powf")))
float __exp10f(float x) {
	return powf(10.0f, x);
};
