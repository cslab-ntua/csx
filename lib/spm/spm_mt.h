/*
 * spm_mt.h -- multithreading spmv methods
 *
 * Copyright (C) 2007-2011, Computing Systems Laboratory (CSLab), NTUA
 * Copyright (C) 2007-2011, Kornilios Kourtis
 * All rights reserved.
 *
 * This file is distributed under the BSD License. See LICENSE.txt for details.
 */
#ifndef __SPM_MT_H__
#define __SPM_MT_H__

struct spm_mt_thread {
	void              *spm;
	void              *spmv_fn;
	unsigned int      cpu;
    void              *data;
};
typedef struct spm_mt_thread spm_mt_thread_t;

struct spm_mt {
	spm_mt_thread_t   *spm_threads;
	unsigned int      nr_threads;
};
typedef struct spm_mt spm_mt_t;

#endif /* __SPM_MT_H__ */
