/*
 * spmv.cc -- Main program for invoking CSX.
 *
 * Copyright (C) 2009-2011, Computing Systems Laboratory (CSLab), NTUA.
 * Copyright (C) 2009-2011, Kornilios Kourtis
 * Copyright (C) 2009-2011, Vasileios Karakasis
 * Copyright (C) 2010-2011, Theodors Goudouvas
 * All rights reserved.
 *
 * This file is distributed under the BSD License. See LICENSE.txt for details.
 */
#include <cstdlib>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <pthread.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

#include "spm.h"
#include "mmf.h"
#include "csx.h"
#include "drle.h"
#include "jit.h"
#include "llvm_jit_help.h"

extern "C" {
#include "mt_lib.h"
#include "spmv_method.h"
#include "spm_crs.h"
#include "spm_mt.h"
#include "spmv_loops_mt.h"
#include "timer.h"
#include "ctl_ll.h"
#include <libgen.h>
}

///> Max deltas that an encoding type may have. This is restricted by the
///  number of bits used for encoding the different patterns in CSX.
///
///  @see ctl_ll.h
#define DELTAS_MAX  CTL_PATTERNS_MAX

// malloc wrapper
#define xmalloc(x)                         \
({                                         \
    void *ret_;                            \
    ret_ = malloc(x);                      \
    if (ret_ == NULL){                     \
        std::cerr << __FUNCTION__          \
                  << " " << __FILE__       \
                  << ":" << __LINE__       \
                  << ": malloc failed\n";  \
        exit(1);                           \
    }                                      \
    ret_;                                  \
})


using namespace csx;

///> Thread data essential for parallel preprocessing
typedef struct thread_info {
    unsigned int thread_no;
    SPM * spm;
    uint64_t wsize;
    std::ostringstream buffer;
    int *xform_buf;
    double sampling_prob;
    uint64_t samples_max;
    bool split_blocks;
    int **deltas;
} thread_info_t;

int SplitString(char *str, char **str_buf, const char *start_sep,
                const char *end_sep)
{
    char *token = strtok(str, start_sep);
    int next = 0;
    int str_length = strcspn(token, end_sep);

    str_buf[next] = (char *) xmalloc((str_length + 1) * sizeof(char));
    strncpy(str_buf[next], token, str_length);
    str_buf[next][str_length] = 0;
    ++next;
    while ((token = strtok(NULL, start_sep)) != NULL) {
        str_length = strcspn(token, end_sep);
        str_buf[next] = (char *) malloc((str_length - 1) * sizeof(char));
        strncpy(str_buf[next], token, str_length);
        str_buf[next][str_length] = 0;
        ++next;
    }

    return next;
}

void GetOptionXform(int **xform_buf)
{
    char *xform_orig = getenv("XFORM_CONF");
    *xform_buf = (int *) xmalloc(XFORM_MAX * sizeof(int));

    if (xform_orig && strlen(xform_orig)) {
        int next = 0;
        int t = atoi(strtok(xform_orig, ","));
        char *token;

        (*xform_buf)[next] = t;
        ++next;
        while ((token = strtok(NULL, ",")) != NULL) {
            t = atoi(token);
            (*xform_buf)[next] = t;
            ++next;
        }

        (*xform_buf)[next] = -1;
    } else {
        (*xform_buf)[0] = 0;
        (*xform_buf)[1] = -1;
    }

    std::cout << "Encoding type: ";
    for (unsigned int i = 0; (*xform_buf)[i] != -1; i++) {
        if (i != 0)
            std::cout << ", ";

        std::cout << SpmTypesNames[(*xform_buf)[i]];
    }

    std::cout << std::endl;
}

void GetOptionEncodeDeltas(int ***deltas)
{
    char *encode_deltas_str = getenv("ENCODE_DELTAS");

    if (encode_deltas_str) {
        // Init matrix deltas.
        *deltas = (int **) xmalloc(XFORM_MAX * sizeof(int *));

        for (int i = 0; i < XFORM_MAX; i++) {
            (*deltas)[i] = (int *) xmalloc(DELTAS_MAX * sizeof(int));
        }

        // Fill deltas with the appropriate data.
        char **temp = (char **) xmalloc(XFORM_MAX * sizeof(char *));
        int temp_size = SplitString(encode_deltas_str, temp, "{", "}");

        for (int i = 0; i < temp_size; i++) {
            int j = 0;
            char *token = strtok(temp[i], ",");

            (*deltas)[i][j] = atoi(token);
            ++j;
            while ((token = strtok(NULL, ",")) != NULL) {
                (*deltas)[i][j] = atoi(token);
                ++j;
            }

            (*deltas)[i][j] = -1;
            free(temp[i]);
        }

        free(temp);

        // Print deltas
        std::cout << "Deltas to Encode: ";
        for (int i = 0; i < temp_size; i++) {
            if (i != 0)
                std::cout << "}, ";
            std::cout << "{";
            assert((*deltas)[i][0] != -1);
            std::cout << (*deltas)[i][0];
            for (int j = 1; (*deltas)[i][j] != -1; j++)
                std::cout << "," << (*deltas)[i][j];
        }

        std::cout << "}" << std::endl;
    }
}

uint64_t GetOptionWindowSize()
{
    const char *wsize_str = getenv("WINDOW_SIZE");
    uint64_t wsize;

    if (!wsize_str) {
        wsize = 0;
        std::cout << "Window size: Not set" << std::endl;
    }
    else {
        wsize = atol(wsize_str);
        std::cout << "Window size: " << wsize << std::endl;
    }

    return wsize;
}

uint64_t GetOptionSamples()
{
    const char *samples = getenv("SAMPLES");
    uint64_t samples_max;

    if (!samples) {
        samples_max = std::numeric_limits<uint64_t>::max();
        std::cout << "Number of samples: Not set" << std::endl;
    }
    else {
        samples_max = atol(samples);
        std::cout << "Number of samples: " << samples_max << std::endl;
    }

    return samples_max;
}

double GetOptionProbability()
{
    const char *sampling_prob_str = getenv("SAMPLING_PROB");
    double sampling_prob;

    if (!sampling_prob_str) {
        sampling_prob = 0.0;
        std::cout << "Sampling prob: Not set" << std::endl;
    }
    else {
        sampling_prob = atof(sampling_prob_str);
        std::cout << "Sampling prob: " << sampling_prob << std::endl;
    }

    return sampling_prob;
}

bool GetOptionSplitBlocks()
{
    const char *split_blocks_str = getenv("SPLIT_BLOCKS");
    bool split_blocks;

    if (!split_blocks_str)
        split_blocks = false;
    else
        split_blocks = true;

    return split_blocks;
}

/**
 *  Parallel Preprocessing.
 *
 *  @param info parameters of matrix needed by each thread.
 */
void *PreprocessThread(void *thread_info)
{
    thread_info_t *data = (thread_info_t *) thread_info;
    DRLE_Manager *DrleMg;

    data->buffer << "==> Thread: #" << data->thread_no << std::endl;

    // Initialize the DRLE manager
    DrleMg = new DRLE_Manager(data->spm, 4, 255-1, 0.1, data->wsize,
                              DRLE_Manager::SPLIT_BY_NNZ, data->sampling_prob,
                              data->samples_max);
    // Adjust the ignore settings properly
    DrleMg->IgnoreAll();
    for (int i = 0; data->xform_buf[i] != -1; ++i)
        DrleMg->RemoveIgnore(static_cast<SpmIterOrder>(data->xform_buf[i]));


     // If the user supplies the deltas choices, encode the matrix with the
     // order given in XFORM_CONF, otherwise find statistical data for the types
     // in XFORM_CONF, choose the best choise, encode it and proceed likewise
     // until there is no satisfying encoding.
    if (data->deltas)
        DrleMg->EncodeSerial(data->xform_buf, data->deltas, data->split_blocks);
    else
        DrleMg->EncodeAll(data->buffer, data->split_blocks);

    // DrleMg->MakeEncodeTree(data->split_blocks);
    delete DrleMg;
    return 0;
}

static spm_mt_t *GetSpmMt(char *mmf_fname)
{
    unsigned int nr_threads, *threads_cpus;
    spm_mt_t *spm_mt;
    SPM *Spms;
    int *xform_buf = NULL;
    int **deltas = NULL;
    thread_info_t *data;
    pthread_t *threads;
    CsxJit **Jits;

    // Get MT_CONF
    mt_get_options(&nr_threads, &threads_cpus);
    std::cout << "MT_CONF=";
    for (unsigned int i = 0; i < nr_threads; i++) {
        if (i != 0)
            std::cout << ",";

        std::cout << threads_cpus[i];
    }

    std::cout << std::endl;

    // Get XFORM_CONF
    GetOptionXform(&xform_buf);

    // Get ENCODE_DELTAS
    GetOptionEncodeDeltas(&deltas);

    // Get WINDOW_SIZE
    uint64_t wsize = GetOptionWindowSize();

    // Get SAMPLES
    uint64_t samples_max = GetOptionSamples();

    // Get SAMPLING_PROB
    double sampling_prob = GetOptionProbability();

    // Get SPLIT_BLOCKS
    bool split_blocks = GetOptionSplitBlocks();

    // Initalization of the multithreaded sparse matrix representation
    spm_mt = (spm_mt_t *) xmalloc(sizeof(spm_mt_t));

    spm_mt->nr_threads = nr_threads;
    spm_mt->spm_threads =
        (spm_mt_thread_t *) xmalloc(sizeof(spm_mt_thread_t) * nr_threads);

    for (unsigned int i = 0; i < nr_threads; i++)
        spm_mt->spm_threads[i].cpu = threads_cpus[i];

    // Load the appropriate sub-matrix to each thread
    Spms = SPM::LoadMMF_mt(mmf_fname, nr_threads);

    // Start timer for preprocessing
    xtimer_t timer;
    timer_init(&timer);
    timer_start(&timer);

    // Initalize and setup threads
    threads = (pthread_t *) xmalloc((nr_threads - 1) * sizeof(pthread_t));

    data = new thread_info_t[nr_threads];
    for (unsigned int i = 0; i < nr_threads; i++) {
        data[i].spm = &Spms[i];
        data[i].wsize = wsize;
        data[i].thread_no = i;
        data[i].xform_buf = xform_buf;
        data[i].sampling_prob = sampling_prob;
        data[i].samples_max = samples_max;
        data[i].split_blocks = split_blocks;
        data[i].deltas = deltas;
        data[i].buffer.str("");
    }

    // Start parallel preprocessing
    for (unsigned int i = 1; i < nr_threads; i++)
        pthread_create(&threads[i-1], NULL, PreprocessThread,
                       (void *) &data[i]);

    PreprocessThread((void *) &data[0]);

    for (unsigned int i = 1; i < nr_threads; ++i)
        pthread_join(threads[i-1],NULL);

    // CSX matrix construction and JIT compilation
    CsxJitInitGlobal();
    Jits = (CsxJit **) xmalloc(nr_threads * sizeof(CsxJit *));

    for (unsigned int i = 0; i < nr_threads; ++i){
        CsxManager *CsxMg = new CsxManager(&Spms[i]);
        spm_mt->spm_threads[i].spm = CsxMg->MakeCsx();
        Jits[i] = new CsxJit(CsxMg, i);
        Jits[i]->GenCode(data[i].buffer);
        delete CsxMg;
        std::cout << data[i].buffer.str();
    }

    // Optimize generated code and assign it to every thread
    CsxJitOptmize();
    for (unsigned int i = 0; i < nr_threads; i++) {
        spm_mt->spm_threads[i].spmv_fn = Jits[i]->GetSpmvFn();
        delete Jits[i];
    }

    // Cleanup.
    free(Jits);
    free(threads);
    delete[] Spms;
    delete[] data;

    // Preprocessing finished; stop timer
    timer_pause(&timer);
    std::cout << "Preprocessing time: "
              << timer_secs(&timer) << " sec"
              << std::endl;
    return spm_mt;
}

static void PutSpmMt(spm_mt_t *spm_mt)
{
    free(spm_mt->spm_threads);
    free(spm_mt);
}

/**
 *  Check the CSX SpMV result against the baseline single-thread CSR
 *  implementation.
 *
 *  @param the (mulithreaded) CSX matrix.
 *  @param the MMF input file.
 */
static void CheckLoop(spm_mt_t *spm_mt, char *mmf_name)
{
    void *crs;
    uint64_t nrows, ncols, nnz;

    crs = spm_crs32_double_init_mmf(mmf_name, &nrows, &ncols, &nnz);
    std::cout << "Checking ... " << std::flush;
    spmv_double_check_mt_loop(crs, spm_mt, spm_crs32_double_multiply, 1, nrows,
                              ncols, NULL);
    spm_crs32_double_destroy(crs);
    std::cout << "Check Passed" << std::endl << std::flush;
}

/**
 *  Compute the size (in bytes) of the compressed matrix in CSX form.
 *
 *  @parame spm_mt  the sparse matrix in CSX format.
 */
static unsigned long CsxSize(spm_mt_t *spm_mt)
{
    unsigned long ret;

    ret = 0;
    for (unsigned int i = 0; i < spm_mt->nr_threads; i++) {
        spm_mt_thread_t *t = spm_mt->spm_threads + i;
        csx_double_t *csx = (csx_double_t *)t->spm;
        ret += csx->nnz * sizeof(double);
        ret += csx->ctl_size;
    }

    return ret;
}

/**
 *  Run CSX SpMV and record the performance information.
 */
static void BenchLoop(spm_mt_t *spm_mt, char *mmf_name)
{
    uint64_t nrows, ncols, nnz;
    double secs, flops;
    long loops_nr = 128;

    getMmfHeader(mmf_name, nrows, ncols, nnz);
    secs = spmv_double_bench_mt_loop(spm_mt, loops_nr, nrows, ncols, NULL);
    flops = (double) (loops_nr*nnz*2) / ((double) 1000*1000*secs);
    printf("m:%s f:%s s:%lu t:%lf r:%lf\n",
           "csx", basename(mmf_name), CsxSize(spm_mt), secs, flops);
}

int main(int argc, char **argv)
{
    spm_mt_t *spm_mt;
    if (argc < 2){
        std::cerr << "Usage: " << argv[0] << " <mmf_file> ... \n";
        exit(1);
    }

    for (int i = 1; i < argc; i++) {
        spm_mt = GetSpmMt(argv[i]);
        CheckLoop(spm_mt, argv[i]);
        std::cerr.flush();
        BenchLoop(spm_mt, argv[i]);
        PutSpmMt(spm_mt);
    }
    return 0;
}


// vim:expandtab:tabstop=8:shiftwidth=4:softtabstop=4
