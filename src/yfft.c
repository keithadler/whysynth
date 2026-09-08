/* WhySynth - real FFT wrappers over KISS FFT, in FFTW halfcomplex layout
 *
 * Copyright (C) 2026 Keith Adler. GPL-2.0-or-later.
 */

#include <stdlib.h>
#include <string.h>

#include "kiss_fftr.h"
#include "yfft.h"

struct yfft_plan {
    int            n;
    int            inverse;
    kiss_fftr_cfg  cfg;
    kiss_fft_cpx  *cpx;    /* n/2 + 1 bins */
    float         *tmp;    /* n reals */
};

static yfft_plan_t *
make_plan(int n, int inverse)
{
    yfft_plan_t *p;

    if (n < 2 || (n & 1)) return NULL;
    p = (yfft_plan_t *)calloc(1, sizeof(yfft_plan_t));
    if (!p) return NULL;
    p->n = n;
    p->inverse = inverse;
    p->cfg = kiss_fftr_alloc(n, inverse, NULL, NULL);
    p->cpx = (kiss_fft_cpx *)malloc((n / 2 + 1) * sizeof(kiss_fft_cpx));
    p->tmp = (float *)malloc(n * sizeof(float));
    if (!p->cfg || !p->cpx || !p->tmp) {
        yfft_destroy(p);
        return NULL;
    }
    return p;
}

yfft_plan_t *yfft_plan_r2hc(int n) { return make_plan(n, 0); }
yfft_plan_t *yfft_plan_hc2r(int n) { return make_plan(n, 1); }

void
yfft_destroy(yfft_plan_t *p)
{
    if (!p) return;
    if (p->cfg) kiss_fftr_free(p->cfg);
    free(p->cpx);
    free(p->tmp);
    free(p);
}

void
yfft_execute_r2hc(yfft_plan_t *p, float *inout)
{
    int n = p->n, k;

    memcpy(p->tmp, inout, n * sizeof(float));
    kiss_fftr(p->cfg, p->tmp, p->cpx);
    /* FFTW R2HC: r0 .. r(n/2), then i((n+1)/2-1) .. i1 */
    for (k = 0; k <= n / 2; k++) inout[k] = p->cpx[k].r;
    for (k = 1; k < (n + 1) / 2; k++) inout[n - k] = p->cpx[k].i;
}

void
yfft_execute_hc2r(yfft_plan_t *p, const float *in, float *out)
{
    int n = p->n, k;

    p->cpx[0].r = in[0];
    p->cpx[0].i = 0.0f;
    for (k = 1; k < n / 2; k++) {
        p->cpx[k].r = in[k];
        p->cpx[k].i = in[n - k];
    }
    p->cpx[n / 2].r = in[n / 2];
    p->cpx[n / 2].i = 0.0f;
    kiss_fftri(p->cfg, p->cpx, out);
}
