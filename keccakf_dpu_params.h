#ifndef KECCAK_DPU_PARAMS_H
#define KECCAK_DPU_PARAMS_H

#include <stdint.h>

struct dpu_params {
    uint32_t fkey; /* first key */
    uint32_t lkey; /* last key */
    uint32_t loops; /* #loops */
};

struct dpu_result {
    uint64_t sum;
    uint64_t cycles;
};

/* Use 16 Tasklets/threads */
#define NR_TASKLETS 16

#endif /* KECCAK_DPU_PARAMS_H */
