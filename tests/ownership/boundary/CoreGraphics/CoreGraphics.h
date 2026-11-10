#ifndef OWNERSHIP_DENIED_COREGRAPHICS_H
#define OWNERSHIP_DENIED_COREGRAPHICS_H
#include <stdlib.h>
typedef void *CGContextRef;
static inline void CGContextScaleCTM(CGContextRef, double, double) { abort(); }
static inline void CGContextTranslateCTM(CGContextRef, double, double) { abort(); }
#endif
