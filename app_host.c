/*
 * Copyright 2018 - UPMEM
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)•
 * any later version.
 *
 */

#include "keccakf_dpu_params.h"
#include <dpu_api.h>
#include <dpu_log.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/time.h>
double dml_micros()
{
    static struct timeval tv;
    gettimeofday(&tv, 0);
    return ((tv.tv_sec * 1000000.0) + tv.tv_usec);
}

static int init_dpus(struct dpu_rank_t **rank)
{
    if (dpu_alloc(NULL, rank) != DPU_API_SUCCESS)
        return -1;

    if (dpu_load_all(*rank, DPU_BINARY) != DPU_API_SUCCESS) {
        dpu_free(*rank);
        return -1;
    }

    return 0;
}

static int run_dpus(struct dpu_rank_t *rank, uint32_t nr_of_dpus)
{
    int res = 0;

    dpu_api_status_t status = dpu_boot_all(rank, SYNCHRONOUS);
    if (status != DPU_API_SUCCESS)
        return -1;

    return res;
}

int main(int argc, char **argv)
{
    const uint64_t fkey = argc > 1 ? atoi(argv[1]) : 0; /* first key */
    const uint64_t lkey = argc > 2 ? atoi(argv[2]) : 1 << 10; /* last  key */
    const uint64_t loops = argc > 3 ? atoi(argv[3]) : 1 << 20; /* #loops    */
    const uint64_t nkey = lkey - fkey;
    struct dpu_rank_t *rank;
    struct dpu_t *dpu;
    uint32_t nr_of_dpus, nr_of_tasklets;
    unsigned int t, i, idx;
    int res = -1;

    if (init_dpus(&rank) != 0) {
        fprintf(stderr, "ERR: cannot initialize DPUs\n");
        return -1;
    }
    if (dpu_get_nr_of_dpus_in(rank, &nr_of_dpus) != DPU_API_SUCCESS)
        goto err;
    nr_of_tasklets = nr_of_dpus * NR_TASKLETS;
    printf("Allocated %d DPU(s) x %d Threads\n", nr_of_dpus, NR_TASKLETS);

    /* send parameters to the DPU tasklets */
    idx = 0;
    DPU_FOREACH (rank, dpu) {
        struct dpu_params params;
        params.loops = loops;
        for (t = 0; t < NR_TASKLETS; t++, idx++) {
            params.fkey = fkey + (nkey * idx + nr_of_tasklets - 1) / nr_of_tasklets;
            params.lkey = fkey + (nkey * (idx + 1) + nr_of_tasklets - 1) / nr_of_tasklets;
            if (params.lkey > lkey)
                params.lkey = lkey;
            printf(" Thread %02d-%02d %d->%d\n", idx / NR_TASKLETS, t, params.fkey, params.lkey);
            if (dpu_tasklet_post(dpu, t, 0, (uint32_t *)&params, sizeof(params)) != DPU_API_SUCCESS) {
                fprintf(stderr, "ERR: cannot write mailbox\n");
                goto err;
            }
        }
    }

    printf("Run program on DPU(s)\n");
    double micros = dml_micros();
    if (run_dpus(rank, nr_of_dpus) != 0) {
        goto err;
        fprintf(stderr, "ERR: cannot execute program correctly\n");
    }
    micros -= dml_micros();

    printf("Retrieve results\n");
    struct dpu_result *results;
    unsigned long long sum = 0;
    uint64_t cycles = 0;
    results = calloc(sizeof(results[0]), nr_of_dpus);
    if (!results)
        goto err;
    i = 0;
    DPU_FOREACH (rank, dpu) {
        /* Retrieve tasklet results and compute the final keccak. */
        for (t = 0; t < NR_TASKLETS; t++) {
            struct dpu_result tasklet_result = { 0 };
            if (dpu_tasklet_receive(dpu, t, 0, (uint32_t *)&tasklet_result, sizeof(tasklet_result)) != DPU_API_SUCCESS) {
                fprintf(stderr, "ERR: cannot receive DPU results correctly\n");
                free(results);
                goto err;
            }
            results[i].sum ^= tasklet_result.sum;
            if (tasklet_result.cycles > results[i].cycles)
                results[i].cycles = tasklet_result.cycles;
        }
        sum ^= results[i].sum;
        if (results[i].cycles > cycles)
            cycles = results[i].cycles;
        i++;
    }
    for (i = 0; i < nr_of_dpus; i++)
        printf("DPU cycle count = %" PRIu64 " cc\n", results[i].cycles);

    double secs = -micros / 1000000.0;
    double Mks = loops * (lkey - fkey) / 1000000.0 / secs;
    printf("_F_ fkey= %6u lkey= %6u loops= %6d SUM= %llx seconds= %1.6lf   Mks= %1.6lf\n", (unsigned int)fkey, (unsigned int)lkey,
        (unsigned int)loops, sum, secs, Mks);

    double dpu_secs = cycles / 600000000.0;
    double dpu_Mks = loops * (lkey - fkey) / 1000000.0 / dpu_secs / nr_of_dpus;
    printf("DPU fkey= %6u lkey= %6u loops= %6d SUM= %llx seconds= %1.6lf   Mks= %1.6lf\n", (unsigned int)fkey, (unsigned int)lkey,
        (unsigned int)loops, sum, dpu_secs, dpu_Mks);

    free(results);
    res = 0;

err:
    if (dpu_free(rank) != DPU_API_SUCCESS) {
        fprintf(stderr, "ERR: cannot free DPUs\n");
        res = -1;
    }

    return res;
}
