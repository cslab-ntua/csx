/*
 * csx_llvm_tmpl.c -- The CSX multiplication template
 *
 * Copyright (C) 2009-2011, Computing Systems Laboratory (CSLab), NTUA.
 * Copyright (C) 2009-2011, Kornilios Kourtis
 * All rights reserved.
 *
 * This file is distributed under the BSD License. See LICENSE.txt for details.
 */
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

/* dont make functions static, so that we can always find them by name */
#define CTL_LL_NOSTATIC
#include "ctl_ll.h"

typedef void llvm_jit_hook_t(void);


#define ALIGN(buf,a) (void *) (((unsigned long) (buf) + (a-1)) & ~(a-1))
void align_ptr(uint8_t **ctl, int align)
{
    *ctl = ALIGN(*ctl, align);
}


llvm_jit_hook_t __new_row_hook;
llvm_jit_hook_t __body_hook;

void
__llvm_annotate(void *val, char *descr, char *file, uint32_t line)
asm("llvm.var.annotation");

#define llvm_annotate(var, name) \
      __llvm_annotate(var, name, __FILE__, __LINE__)

void inline fail()
{
    assert(0);
}

void print_yx(uint64_t y, uint64_t x)
{
    printf("%" PRIu64 " %" PRIu64 "\n", y+1, x+1);
}

void print_yxv(uint64_t y, uint64_t x, double v)
{
    printf("%" PRIu64 " %" PRIu64 " %le\n", y+1, x+1, v);
}

#define ELEM_TYPE double
#include "vector.h"
#include "csx.h"

void csx_spmv_template(void *spm, vector_double_t *in, vector_double_t *out)
{
    csx_double_t *csx = (csx_double_t *)spm;
    double *x;
    double *y;
    double *v = csx->values;
    double *myx;
    double yr = 0;
    uint8_t *ctl = csx->ctl;
    uint8_t *ctl_end = ctl + csx->ctl_size;
    uint64_t y_indx = csx->row_start;
    uint8_t size, flags;
    uint64_t i;

    //printf("csx->ctl: %p\n", csx->ctl);
    x = in  ? in->elements : NULL;
    y = out ? out->elements: NULL;
    myx = x;
    
    llvm_annotate(&yr, "spmv::yr");
    llvm_annotate(&myx, "spmv::myx");
    llvm_annotate(&x, "spmv::x");
    llvm_annotate(&y, "spmv::y");
    llvm_annotate(&y_indx, "spmv::y_indx");
    llvm_annotate(&v, "spmv::v");
    llvm_annotate(&ctl, "spmv::ctl");
    llvm_annotate(&size, "spmv::size");
    llvm_annotate(&flags, "spmv::flags");
    
    for (i = csx->row_start; i < csx->row_start + csx->nrows; i++)
	    y[i] = 0;

    do {
        //printf("ctl:%p\n", ctl);
        flags = *ctl++;
        size = *ctl++;
        //printf("size=%d\n", size);
        if (test_bit(&flags, CTL_NR_BIT)){
            __new_row_hook();
            myx = x;
            yr = 0;
            //y[y_indx] = yr;
        }
        //printf("x_indx before jmp: %lu\n", myx - x);
        myx += ul_get(&ctl);
        //printf("x_indx after jmp: %lu\n", myx - x);
        __body_hook();
        //printf("x_indx at end: %lu\n", myx - x);
    } while (ctl < ctl_end);
    y[y_indx] += yr;
}

void csx_sym_spmv_template(void *spm, vector_double_t *in, vector_double_t *out,
                           vector_double_t *tmp)
{
    csx_double_sym_t *csx_sym = (csx_double_sym_t *) spm;
    csx_double_t *csx = (csx_double_t *) csx_sym->lower_matrix;
    double *x;
    double *y;
    double *temp;
    double *cur;
    double *v = csx->values;
    double *dv = csx_sym->dvalues;
    double *myx;
    double yr = 0;
    uint8_t *ctl = csx->ctl;
    uint8_t *ctl_end = ctl + csx->ctl_size;
    uint64_t y_indx = csx->row_start;
    uint64_t y_end = csx->row_start + csx->nrows;
    uint8_t size, flags;
    uint64_t i, curx;
            
    //printf("csx->ctl: %p\n", csx->ctl);

    x = in  ? in->elements : NULL;
    y = out ? out->elements : NULL;
    temp = tmp ? tmp->elements : NULL;
    myx = x;
    cur = temp;
    
    llvm_annotate(&yr, "spmv::yr");
    llvm_annotate(&myx, "spmv::myx");
    llvm_annotate(&x, "spmv::x");
    llvm_annotate(&y, "spmv::y");
    llvm_annotate(&y_indx, "spmv::y_indx");
    llvm_annotate(&v, "spmv::v");
    llvm_annotate(&dv, "spmv::dv");
    llvm_annotate(&ctl, "spmv::ctl");
    llvm_annotate(&size, "spmv::size");
    llvm_annotate(&flags, "spmv::flags");
    llvm_annotate(&cur, "spmv::cur");
    
    for (i = csx->row_start; i < csx->row_start + csx->nrows; i++)
        y[i] = 0;
    
    do {
        flags = *ctl++;
        size = *ctl++;
        if (test_bit(&flags, CTL_NR_BIT)) {
            __new_row_hook();
            myx = x;
            yr = 0;
            cur = temp;
        }
        myx += ul_get(&ctl);
        curx = myx - x;
        if (curx >= csx->row_start)
            cur = y;
        __body_hook();
    } while (ctl < ctl_end);
    y[y_indx] += yr + x[y_indx] * dv[0];
    y_indx++;
    while (y_indx < y_end) {
        dv++;
        y[y_indx] += x[y_indx] * dv[0];
        y_indx++;
    }
}

// vim:expandtab:tabstop=8:shiftwidth=4:softtabstop=4
