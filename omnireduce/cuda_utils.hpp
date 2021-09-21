#include <cuda_runtime.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <curand.h>

void compute_bitmap(float* d_tensor, uint8_t* d_bitmap, int64_t tensor_size, uint32_t block_size, cudaStream_t stream, float threshold);
void compute_bitmap(int* d_tensor, uint8_t* d_bitmap, int64_t tensor_size, uint32_t block_size, cudaStream_t stream, int threshold);
void compute_bitmap(uint8_t* d_tensor, uint8_t* d_bitmap, int64_t tensor_size, uint32_t block_size, cudaStream_t stream, uint8_t threshold);
void cnat_compress(float* input, uint8_t* output, int count, cudaStream_t stream, curandGenerator_t* gen);
void cnat_decompress(uint8_t* input, float* output, int count, cudaStream_t stream);
