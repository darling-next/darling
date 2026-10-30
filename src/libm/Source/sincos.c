#include "math.h"

/*
 * These functions implement Clang's Darwin sin/cos-combination libcalls.
 * Do not let the optimizer combine their scalar backend calls back into
 * __sincos[f]_stret: that would recurse instead of computing either result.
 * Keep the scalar libm implementations and the public aggregate return ABI.
 */
__attribute__((no_builtin("sinf", "cosf")))
struct __float2 __sincosf_stret(float v)
{
	struct __float2 rv = {
		sinf(v), cosf(v)
	};
	return rv;
}

__attribute__((no_builtin("sin", "cos")))
struct __double2 __sincos_stret(double v)
{
	struct __double2 rv = {
		sin(v), cos(v)
	};
	return rv;
}

struct __float2 __sincospif_stret(float v)
{
	return __sincosf_stret(v * M_PI);
}

struct __double2 __sincospi_stret(double v)
{
	return __sincos_stret(v * M_PI);
}
