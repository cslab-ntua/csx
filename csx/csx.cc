/*
 * csx.cc -- The CSX Manager implementation
 *
 * Copyright (C) 2009-2011, Computing Systems Laboratory (CSLab), NTUA.
 * Copyright (C) 2009-2011, Kornilios Kourtis
 * Copyright (C) 2010-2012, Theodoros Gkountouvas
 * Copyright (C) 2011-2012, Vasileios Karakasis
 * All rights reserved.
 *
 * This file is distributed under the BSD License. See LICENSE.txt for details.
 */
#include <map>
#include <algorithm>

#include <boost/foreach.hpp>

#define FOREACH BOOST_FOREACH

#include "dynarray.h"
#include "spm.h"
#include "delta.h"
#include "csx.h"

#ifdef SPM_NUMA
#   include <numa.h>
#   include "numa_util.h"
#   define DYNARRAY_CREATE  dynarray_create_numa
#else
#   define DYNARRAY_CREATE  dynarray_create
#endif

void DestroyCsx(csx_double_t *csx)
{
#ifdef SPM_NUMA
    free_interleaved(csx->ctl, csx->ctl_size*sizeof(*csx->ctl));
    free_interleaved(csx->values, csx->nnz*sizeof(*csx->values));
    free_interleaved(csx, sizeof(*csx));
#else
    free(csx->ctl);
    free(csx->values);
    free(csx);
#endif
}

void DestroyCsxSym(csx_double_sym_t *csx_sym)
{
#ifdef SPM_NUMA
    uint64_t diag_size = csx_sym->lower_matrix->nrows;
#endif    
        DestroyCsx(csx_sym->lower_matrix);
#ifdef SPM_NUMA
    numa_free(csx_sym->dvalues, diag_size*sizeof(*csx_sym->dvalues));
    numa_free(csx_sym, sizeof(*csx_sym));
#else
    free(csx_sym->dvalues);
    free(csx_sym);
#endif
}

using namespace csx;

static bool debug = false;

template<typename IterT, typename ValT>
void DeltaEncode(IterT start, IterT end, ValT &x0)
{
    IterT i;
    ValT prev, tmp;

    prev = x0;
    for (i = start; i != end; ++i){
        tmp = *i;
        *i -= prev;
        prev = tmp;
    }
}

template<typename T>
void Copy(T *dst, uint64_t *src, long nr_items)
{
    for (long i = 0; i < nr_items; i++){
        dst[i] = static_cast<T>(src[i]);
    }
}


uint8_t CsxManager::GetFlag(long pattern_id, uint64_t nnz)
{
    CsxManager::PatMap::iterator pi;
    uint8_t ret;

    pi = this->patterns.find(pattern_id);
    if (pi == this->patterns.end()) {
        ret = flag_avail_++;
        assert(ret <= CTL_PATTERNS_MAX && "too many patterns");
        CsxManager::PatInfo pat_info(ret, 1, nnz);
        this->patterns[pattern_id] = pat_info;
    } else {
        ret = pi->second.flag;
        pi->second.npatterns++;
        pi->second.nr += nnz;
    }

    return ret;
}

csx_double_sym_t *CsxManager::MakeCsxSym()
{
    csx_double_sym_t *csx;
    double *diagonal = spm_sym_->GetDiagonal();
    uint64_t diagonal_size = spm_sym_->GetDiagonalSize();
    
    spm_ = spm_sym_->GetLowerMatrix();

#ifdef SPM_NUMA
    int cpu = sched_getcpu();
    if (cpu < 0) {
        perror("sched_getcpu() failed");
        exit(1);
    }

    int node = numa_node_of_cpu(cpu);
    if (node < 0) {
        perror("numa_node_of_cpu() failed");
        exit(1);
    }

    csx = (csx_double_sym_t *) numa_alloc_onnode(sizeof(csx_double_sym_t),
                                                 node);
    csx->dvalues = (double *) numa_alloc_onnode(diagonal_size * sizeof(double),
                                                node);
#else  
    csx = (csx_double_sym_t *) malloc(sizeof(csx_double_sym_t));
    csx->dvalues = (double *) malloc(diagonal_size * sizeof(double));
#endif

    for (uint64_t i = 0; i < diagonal_size; i++)
        csx->dvalues[i] = diagonal[i];
    csx->lower_matrix = MakeCsx(true);
    
    return csx;
}

csx_double_t *CsxManager::MakeCsx(bool symmetric)
{
    csx_double_t *csx;

#ifdef SPM_NUMA
    int cpu = sched_getcpu();
    if (cpu < 0) {
        perror("sched_getcpu() failed");
        exit(1);
    }

    int node = numa_node_of_cpu(cpu);
    if (node < 0) {
        perror("numa_node_of_cpu() failed");
        exit(1);
    }

    csx = (csx_double_t *) alloc_onnode(sizeof(csx_double_t), node);
    values_ = (double *) alloc_onnode(sizeof(double)*spm_->nr_nzeros_,
                                      node);
#else    
    csx = (csx_double_t *) malloc(sizeof(csx_double_t));
    values_ = (double *) malloc(sizeof(double)*spm_->nr_nzeros_);
#endif  // SPM_NUMA
    if (!csx || !values_) {
        std::cerr << __FUNCTION__ << ": malloc failed\n";
        exit(1);
    }

    // Be greedy with the initial capacity (equal to CSR col_ind size) 
    // to avoid realloc()'s.
    ctl_da_ = DYNARRAY_CREATE(sizeof(uint8_t), 512, 6*spm_->nr_nzeros_);
    csx->nnz = spm_->nr_nzeros_;
    csx->nrows = spm_->nr_rows_;
    csx->ncols = spm_->nr_cols_;
    csx->row_start = spm_->row_start_;
    values_idx_ = 0;
    new_row_ = false;		        // Do not mark first row.

    if (!symmetric) {
        for (uint64_t i = 0; i < spm_->GetNrRows(); i++) {
            const SpmRowElem *rbegin, *rend;
    
            rbegin = spm_->RowBegin(i);
            rend = spm_->RowEnd(i);
            if (debug)
                std::cerr << "MakeCsx(): row: " << i << "\n";
    
            if (rbegin == rend){		// Check if row is empty.
                if (debug)
                    std::cerr << "MakeCsx(): row is empty" << std::endl;
    
                if (new_row_ == false)
                    new_row_ = true;	// In case the first row is empty.
                else
                    empty_rows_++;
    
                continue;
            }
    
            DoRow(rbegin, rend);
            new_row_ = true;
        }
    } else {
        for (uint64_t i = 0; i < spm_->GetNrRows(); i++) {
            const SpmRowElem *rbegin, *rend;
    
            rbegin = spm_->RowBegin(i);
            rend = spm_->RowEnd(i);
            if (debug)
                std::cerr << "MakeCsx(): row: " << i << "\n";
    
            if (rbegin == rend){		// Check if row is empty.
                if (debug)
                    std::cerr << "MakeCsx(): row is empty" << std::endl;
    
                if (new_row_ == false)
                    new_row_ = true;	// In case the first row is empty.
                else
                    empty_rows_++;
    
                continue;
            }
    
            DoSymRow(rbegin, rend);
            new_row_ = true;
        }
    }

    csx->ctl_size = dynarray_size(ctl_da_);
    csx->ctl = (uint8_t *) dynarray_destroy(ctl_da_);
    ctl_da_ = NULL;
    assert(values_idx_ == spm_->nr_nzeros_);
    csx->values = values_;
    values_ = NULL;
    values_idx_ = 0;
    return csx;
}

/*
 *  Ctl Rules
 *  1. Each unit leaves the x index at the last element it calculated on the
 *     current row.
 *  2. Size is the number of elements that will be calculated.
 */
void CsxManager::DoRow(const SpmRowElem *rbegin, const SpmRowElem *rend)
{
    std::vector<uint64_t> xs;

    last_col_ = 1;
    for (const SpmRowElem *spm_elem = rbegin; spm_elem < rend; spm_elem++) {
        if (debug)
            std::cerr << "\t" << *spm_elem << "\n";

        // Check if this element contains a pattern.
        if (spm_elem->pattern != NULL) {
            PreparePat(xs, *spm_elem);
            assert(xs.size() == 0);
            AddPattern(*spm_elem);
            for (long i = 0; i < spm_elem->pattern->GetSize(); i++)
                values_[values_idx_++] = spm_elem->vals[i];

            continue;
        }

        // Check if we exceeded the maximum size for a unit.
        assert(xs.size() <= CTL_SIZE_MAX);
        if (xs.size() == CTL_SIZE_MAX)
             AddXs(xs);

        xs.push_back(spm_elem->x);
        values_[values_idx_++] = spm_elem->val;
    }

    if (xs.size() > 0)
        AddXs(xs);
}

/*
 *  Ctl Rules
 *  1. Each unit leaves the x index at the last element it calculated on the
 *     current row.
 *  2. Size is the number of elements that will be calculated.
 */
void CsxManager::DoSymRow(const SpmRowElem *rbegin, const SpmRowElem *rend)
{
    std::vector<uint64_t> xs;
    const SpmRowElem *spm_elem = rbegin;

    last_col_ = 1;
    for ( ; spm_elem < rend && spm_elem->x < spm_->GetRowStart() + 1; 
         spm_elem++) {
        if (debug)
            std::cerr << "\t" << *spm_elem << "\n";

        // Check if this element contains a pattern.
        if (spm_elem->pattern != NULL) {
            PreparePat(xs, *spm_elem);
            assert(xs.size() == 0);
            AddPattern(*spm_elem);
            for (long i=0; i < spm_elem->pattern->GetSize(); i++)
                values_[values_idx_++] = spm_elem->vals[i];

            continue;
        }

        // Check if we exceeded the maximum size for a unit.
        assert(xs.size() <= CTL_SIZE_MAX);
        if (xs.size() == CTL_SIZE_MAX)
             AddXs(xs);

        xs.push_back(spm_elem->x);
        values_[values_idx_++] = spm_elem->val;
    }

    if (xs.size() > 0)
        AddXs(xs);

    for ( ; spm_elem < rend; spm_elem++) {
        if (debug)
            std::cerr << "\t" << *spm_elem << "\n";

        // Check if this element contains a pattern.
        if (spm_elem->pattern != NULL) {
            PreparePat(xs, *spm_elem);
            assert(xs.size() == 0);
            AddPattern(*spm_elem);
            for (long i=0; i < spm_elem->pattern->GetSize(); i++)
                values_[values_idx_++] = spm_elem->vals[i];

            continue;
        }

        // Check if we exceeded the maximum size for a unit.
        assert(xs.size() <= CTL_SIZE_MAX);
        if (xs.size() == CTL_SIZE_MAX)
             AddXs(xs);

        xs.push_back(spm_elem->x);
        values_[values_idx_++] = spm_elem->val;
    }

    if (xs.size() > 0)
        AddXs(xs);
}

// Note that this function may allocate space in ctl_da.
void CsxManager::UpdateNewRow(uint8_t *flags)
{
	if (!new_row_)
		return;

	set_bit(flags, CTL_NR_BIT);
	new_row_ = false;
	if (empty_rows_ != 0){
		set_bit(flags, CTL_RJMP_BIT);
		da_put_ul(ctl_da_, empty_rows_ + 1);
		empty_rows_ = 0;
		row_jmps_ = true;
	}
}

void CsxManager::AddXs(std::vector<uint64_t> &xs)
{
    uint8_t *ctl_flags, *ctl_size;
    long pat_id, xs_size, delta_bytes;
    uint64_t last_col, max;
    DeltaSize delta_size;
    std::vector<uint64_t>::iterator vi;
    void *dst;

    // Do delta encoding.
    xs_size = xs.size();
    last_col = xs[xs_size-1];
    uint64_t x_start = xs[0];
    DeltaEncode(xs.begin(), xs.end(), last_col_);
    last_col_ = last_col;

    // Calculate the delta's size and the pattern id.
    max = 0;
    if (xs_size > 1) {
        vi = xs.begin();
        std::advance(vi, 1);
        max = *(std::max_element(vi, xs.end()));
    }
    delta_size =  getDeltaSize(max);
    pat_id = (8<<delta_size) + PID_DELTA_BASE;

    // Set flags.
    ctl_flags = (uint8_t *) dynarray_alloc_nr(ctl_da_, 2);
    *ctl_flags = GetFlag(PID_DELTA_BASE + pat_id, xs_size);

    // Set size.
    ctl_size = ctl_flags + 1;
    assert( (xs_size > 0) && (xs_size <= CTL_SIZE_MAX));
    *ctl_size = xs_size;

    // Variables ctls_size, ctl_flags are not valid after this call.
    UpdateNewRow(ctl_flags);

    // Add the column index
    if (full_column_indices_)
        da_put_u32(ctl_da_, x_start-1);
    else
        da_put_ul(ctl_da_, xs[0]);

    // Add deltas (if needed).
    if (xs_size > 1) {
        delta_bytes = DeltaSize_getBytes(delta_size);
        dst = dynarray_alloc_nr_aligned(ctl_da_, delta_bytes*(xs_size-1),
                                        delta_bytes);
        switch (delta_size) {
        case DELTA_U8:
            Copy((uint8_t  *) dst, &xs[1], xs_size-1);
            break;
        case DELTA_U16:
            Copy((uint16_t *) dst, &xs[1], xs_size-1);
            break;
        case DELTA_U32:
            Copy((uint32_t *) dst, &xs[1], xs_size-1);
            break;
        default:
            assert(false);
	    }
    }

    xs.clear();
    return;
}

void CsxManager::AddPattern(const SpmRowElem &elem)
{
    uint8_t *ctl_flags, *ctl_size;
    long pat_size, pat_id;
    uint64_t ucol;

    pat_size = elem.pattern->GetSize();
    pat_id = elem.pattern->GetPatternId();
    ctl_flags = (uint8_t *) dynarray_alloc_nr(ctl_da_, 2);
    *ctl_flags = GetFlag(pat_id, pat_size);
    ctl_size = ctl_flags + 1;
    assert(pat_size <= CTL_SIZE_MAX);
    *ctl_size = pat_size;
    UpdateNewRow(ctl_flags);

    if (full_column_indices_)
        ucol = elem.x;
    else
        ucol = elem.x - last_col_;
        
    if (debug)
        std::cerr << "AddPattern ujmp " << ucol << "\n";

    if (full_column_indices_)
        da_put_u32(ctl_da_, ucol-1);
    else
        da_put_ul(ctl_da_, ucol);

    last_col_ = elem.pattern->ColIncreaseJmp(spm_->type_, elem.x);
    if (debug)
        std::cerr << "last_col:" << last_col_ << "\n";
}

// return ujmp
void CsxManager::PreparePat(std::vector<uint64_t> &xs, const SpmRowElem &elem)
{
    if (xs.size() != 0)
        AddXs(xs);
}

// vim:expandtab:tabstop=8:shiftwidth=4:softtabstop=4
