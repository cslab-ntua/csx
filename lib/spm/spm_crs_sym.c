/*
 * spm_crs_sym.c -- SSS format for symmetric sparse matrices.
 *
 * Copyright (C) 2011-2012, Computing Systems Laboratory (CSLab), NTUA
 * Copyright (C) 2011-2012, Theodoros Gkountouvas
 * All rights reserved.
 *
 * This file is distributed under the BSD License. See LICENSE.txt for details.
 */
#include "spm_crs_sym.h"

void *SPM_CRS_SYM_NAME(_init_mmf)(char *mmf_file, uint64_t *nrows,
                                  uint64_t *ncols, uint64_t *nnz,
                                  void *metadata)
{
	SPM_CRS_SYM_TYPE *crs;

	crs = (SPM_CRS_SYM_TYPE *) malloc(sizeof(SPM_CRS_SYM_TYPE));
	if (!crs) {
		perror("malloc");
		exit(1);
	}

	FILE *mmf = mmf_init(mmf_file, nrows, ncols, nnz);
	assert(*nrows == *ncols);
	assert((*nnz - *nrows) % 2 == 0);

	// Allocation for SSS format.
	crs->n = *nrows;
	crs->nnz = (*nnz - *nrows) / 2;
	crs->dvalues = malloc(crs->n*sizeof(*crs->dvalues));
	crs->values = malloc(crs->nnz*sizeof(*crs->values));
	crs->col_ind = malloc(crs->nnz * sizeof(*crs->col_ind));
	crs->row_ptr = malloc((crs->n+1)*sizeof(*crs->row_ptr));
	if (!crs->dvalues || !crs->values || !crs->col_ind || !crs->row_ptr) {
		perror("malloc");
		exit(1);
	}

	uint64_t row, col, row_prev;
	double val;
	uint64_t row_i=0, val_i=0;

	crs->row_ptr[row_i++] = val_i;
	row_prev = 0;
	while (mmf_get_next(mmf, &row, &col, &val)) {
		// Elements that belong to the upper triangle matrix are ignored.
		if (col > row)
			continue;

		/* Elements that belong to the main diagonal are stored 
		   to dvalues field. */
		if (col == row) {
			crs->dvalues[row] = (ELEM_TYPE) val;
			continue;
		}

		// Sanity checks for symmetric matrices.
		assert(col < row);
		assert(row >= row_prev);
		assert(row < crs->n);
		assert(col < crs->n);
		assert(val_i < crs->nnz);

		/* Elements that belong to the lower triangle matrix are stored in CSR
		   format. */
		if (row != row_prev) {
			uint64_t i;

			for (i = 0; i < row - row_prev; i++)
				crs->row_ptr[row_i++] = val_i;
			row_prev = row;
		}

		crs->values[val_i] = val;
		crs->col_ind[val_i] = col;
		val_i++;
	}

	crs->row_ptr[row_i++] = val_i;

	assert(row_i == crs->n + 1);
	assert(val_i == crs->nnz);

	fclose(mmf);

	return crs;
}

void SPM_CRS_SYM_NAME(_destroy)(void *spm)
{
	SPM_CRS_SYM_TYPE *crs = (SPM_CRS_SYM_TYPE *) spm;

	free(crs->dvalues);
	free(crs->values);
	free(crs->col_ind);
	free(crs->row_ptr);
	free(crs);
}

uint64_t SPM_CRS_SYM_NAME(_size)(void *spm)
{
	uint64_t ret;
	SPM_CRS_SYM_TYPE *crs = (SPM_CRS_SYM_TYPE *) spm;

	ret = (crs->nnz + crs->n) * sizeof(ELEM_TYPE)
	    + (crs->nnz + crs->n + 1) * sizeof(SPM_CRS_IDX_TYPE);

	return ret;
}

void SPM_CRS_SYM_NAME(_multiply) (void *spm, VECTOR_TYPE *in, VECTOR_TYPE *out)
{
	SPM_CRS_SYM_TYPE *crs = (SPM_CRS_SYM_TYPE *) spm;
	ELEM_TYPE *y = out->elements;
	ELEM_TYPE *x = in->elements;
	ELEM_TYPE *values = crs->values;
	ELEM_TYPE *dvalues = crs->dvalues;
	SPM_CRS_IDX_TYPE *row_ptr = crs->row_ptr;
	SPM_CRS_IDX_TYPE *col_ind = crs->col_ind;
	uint64_t n = crs->n;
	register ELEM_TYPE yr;
	uint64_t i, j;

	for (i = 0; i < n; i++) {
		yr  = (ELEM_TYPE) 0;
		for(j = row_ptr[i]; j < row_ptr[i+1]; j++) {
			yr += values[j] * x[col_ind[j]];
			y[col_ind[j]] += values[j] * x[i];
		}

		yr += dvalues[i] * x[i];
		y[i] = yr;
	}
}

XSPMV_SYM_METH_INIT(
 SPM_CRS_SYM_NAME(_multiply),
 SPM_CRS_SYM_NAME(_init_mmf),
 SPM_CRS_SYM_NAME(_size),
 SPM_CRS_SYM_NAME(_destroy),
 sizeof(ELEM_TYPE)
)
