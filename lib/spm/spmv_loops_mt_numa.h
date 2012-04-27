/*
 * spmv_loops_mt_numa.h -- NUMA-aware SpMV benchmark routines
 *
 * Copyright (C) 2011-2012, Computing Systems Laboratory (CSLab), NTUA
 * Copyright (C) 2011-2012, Vasileios Karakasis
 * All rights reserved.
 *
 * This file is distributed under the BSD License. See LICENSE.txt for details.
 */

#ifndef __SPMV_LOOPS_MT_NUMA_H__
#define __SPMV_LOOPS_MT_NUMA_H__

#include "spmv_method.h"
#include "spm_mt.h"
#include "numa_util.h"

float spmv_double_bench_mt_loop_numa(spm_mt_t *spm_mt, unsigned long loops,
                                     unsigned long rows_nr,
                                     unsigned long cols_nr,
                                     SPMV_NAME(_fn_t) *fn);

float spmv_float_bench_mt_loop_numa(spm_mt_t *spm_mt, unsigned long loops,
                                    unsigned long rows_nr,
                                    unsigned long cols_nr,
                                    SPMV_NAME(_fn_t) *fn);

void spmv_double_check_mt_loop_numa(void *spm, spm_mt_t *spm_mt,
                                    SPMV_NAME(_fn_t) *fn, unsigned long loops,
                                    unsigned long rows_nr,
                                    unsigned long cols_nr,
                                    SPMV_NAME(_fn_t) *mt_fn);

void spmv_float_check_mt_loop_numa(void *spm, spm_mt_t *spm_mt,
                                   SPMV_NAME(_fn_t) *fn, unsigned long loops,
                                   unsigned long rows_nr,
                                   unsigned long cols_nr,
                                   SPMV_NAME(_fn_t) *mt_fn);

#endif  /* SPMV_LOOPS_MT_NUMA_H__ */
