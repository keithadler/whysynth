/* WhySynth - real FFT wrappers over KISS FFT, in FFTW halfcomplex layout
 *
 * Copyright (C) 2026 Keith Adler. GPL-2.0-or-later.
 *
 * padsynth.c was written against FFTW's r2r transforms. These wrappers keep
 * its halfcomplex array layout (r0, r1, ..., r(n/2), i((n+1)/2-1), ..., i1)
 * and unnormalized scaling, so padsynth.c changes only its calls, and the
 * plugin no longer depends on FFTW.
 */

#ifndef _YFFT_H
#define _YFFT_H

typedef struct yfft_plan yfft_plan_t;

/* n must be even (KISS FFT's real transforms need that); any even n works */
yfft_plan_t *yfft_plan_r2hc(int n);
yfft_plan_t *yfft_plan_hc2r(int n);
void         yfft_destroy(yfft_plan_t *plan);

/* real to halfcomplex, in place */
void yfft_execute_r2hc(yfft_plan_t *plan, float *inout);
/* halfcomplex to real, out of place; output scaled by n like FFTW */
void yfft_execute_hc2r(yfft_plan_t *plan, const float *in, float *out);

#endif /* _YFFT_H */
