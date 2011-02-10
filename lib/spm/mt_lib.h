/*
 * mt_lib.h -- multithreading helpers
 *
 * Copyright (C) 2007-2011, Computing Systems Laboratory (CSLab), NTUA
 * Copyright (C) 2007-2011, Kornilios Kourtis
 * All rights reserved.
 *
 * This file is distributed under the BSD License. See LICENSE.txt for details.
 */
#ifndef __MT_LIB_H__
#define __MT_LIB_H__

void setaffinity_oncpu(unsigned int cpu);
void mt_get_options(unsigned int *nr_cpus, unsigned int **cpus);

#endif
