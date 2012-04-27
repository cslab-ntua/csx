/*
 * csx_spmv_tmpl.c -- The CSX-Sym multiplication template
 *
 * Copyright (C) 2011-2012, Computing Systems Laboratory (CSLab), NTUA.
 * Copyright (C) 2011-2012, Theodoros Gkountouvas
 * All rights reserved.
 *
 * This file is distributed under the BSD License. See LICENSE.txt for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>

#include "ctl_ll.h"

#define ELEM_TYPE double
#include "vector.h"
#include "csx.h"

#define CSX_SPMV_FN_MAX CTL_PATTERNS_MAX

#define ALIGN(buf,a) (void *) (((unsigned long) (buf) + (a-1)) & ~(a-1))

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
// the following function will be unused when only delta8 units
// are present
static void align_ptr(uint8_t **ctl, int align)
{
	*ctl = ALIGN(*ctl, align);
}
#pragma GCC diagnostic pop

#ifdef CSX_DEBUG
static void ctl_print(uint8_t *ctl, uint64_t start, uint64_t end,
                      const char *descr)
{
	for (uint64_t i = start; i < end; i++)
		printf("%s: ctl[%ld] = %d\n", descr, i, ctl[i]);
}

static void deref(void *ptr)
{
	volatile unsigned long val = *((unsigned long *) ptr);
	val++;
}
#endif

${spmv_func_definitions}

void spm_csx32_double_sym_multiply(void *spm, vector_double_t *in,
                                   vector_double_t *out,
                                   vector_double_t *temp)
{
	csx_double_sym_t *csx_sym = (csx_double_sym_t *) spm;
	csx_double_t *csx = csx_sym->lower_matrix;
	double *x = in->elements;
	double *y = out->elements;
	double *tmp = temp->elements;
	double *v = csx->values;
	double *dv = csx_sym->dvalues;
	uint64_t x_indx = 0;
	uint64_t y_indx = csx->row_start;
	uint64_t y_end = csx->row_start + csx->nrows;
	register double yr = 0;
	uint8_t *ctl = csx->ctl;
	uint8_t *ctl_end = ctl + csx->ctl_size;
	uint8_t flags, size, patt_id;
	uint64_t i;
	double *cur = tmp;

	for (i = y_indx; i < y_end; i++)
		y[i] = 0;

	do {
		flags = *ctl++;
		size = *ctl++;
		if (test_bit(&flags, CTL_NR_BIT)) {
			y[y_indx] += yr;
			${new_row_hook}
			yr = 0;
			x_indx = 0;
			// Switch Reduction Phase
			cur = tmp;
		}
		
		${next_x}
		// Switch Reduction Phase
		if (cur != y && x_indx >= csx->row_start)
			cur = y;
		patt_id = flags & CTL_PATTERN_MASK;
		${body_hook}
	} while (ctl < ctl_end);
	
	y[y_indx] += yr;
	
	for (i = y_indx; i < y_end; i++) {
		y[i] += x[i] * (*dv);
		dv++;
	}
}
