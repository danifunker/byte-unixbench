/*******************************************************************************
 *  UnixBench portable harness - sincos() for Solaris 8
 *
 *  The compiler (a GCC that targets Linux, see Makefile.sparc) turns
 *  Whetstone's sin(x) and cos(x) of one argument into a single sincos(), a
 *  GNU function that Solaris 8's libm does not have.  Here it is, as the two
 *  calls into Solaris's libm that the source asked for.  Makefile.sparc
 *  builds this file with -fno-builtin, or the compiler would turn the body
 *  into a call to sincos() again.
 ******************************************************************************/

#include <math.h>

void sincos(double x, double *s, double *c);

void sincos(double x, double *s, double *c)
{
    *s = sin(x);
    *c = cos(x);
}
