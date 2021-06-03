#include "cuda_utils.hpp"
template <typename scalar_t>
__global__ void bitmap_cuda_kernel(scalar_t* input, uint8_t* bitmap, int64_t len, scalar_t threshold) {
    const auto index = blockIdx.x * blockDim.x + threadIdx.x;
    __shared__ bool zero_block;
    if (threadIdx.x == 0) zero_block = true;
    __syncthreads();
    if(index < len) {
      if(std::abs(input[index]) > threshold) zero_block=false;
    }
    __syncthreads();
    if(index < len) {
      if(zero_block) {
        input[index]=(scalar_t)0;
        bitmap[blockIdx.x]=1;
      }
      else {
        bitmap[blockIdx.x]=0;
      }
    }
    __syncthreads();
}

void compute_bitmap(float* d_tensor, uint8_t* d_bitmap, int64_t tensor_size, uint32_t block_size, cudaStream_t stream, float threshold) {
    uint32_t block_num = tensor_size/block_size;
    if (tensor_size%block_size!=0)
        block_num += 1;
    bitmap_cuda_kernel<<<block_num, block_size, 0, stream>>>(d_tensor, d_bitmap, tensor_size, threshold);
}

void compute_bitmap(int* d_tensor, uint8_t* d_bitmap, int64_t tensor_size, uint32_t block_size, cudaStream_t stream, int threshold) {
  uint32_t block_num = tensor_size/block_size;
  if (tensor_size%block_size!=0)
      block_num += 1;
  bitmap_cuda_kernel<<<block_num, block_size, 0, stream>>>(d_tensor, d_bitmap, tensor_size, threshold);
}

#ifdef USE_CNAT
void compute_bitmap(uint8_t* d_tensor, uint8_t* d_bitmap, int64_t tensor_size, uint32_t block_size, cudaStream_t stream, uint8_t threshold) {
  uint32_t block_num = tensor_size/block_size;
  if (tensor_size%block_size!=0)
      block_num += 1;
  bitmap_cuda_kernel<<<block_num, block_size, 0, stream>>>(d_tensor, d_bitmap, tensor_size, threshold);
}

__global__ void cnat_compress_cuda_kernel(
    float* __restrict__ input,
    uint8_t* __restrict__ output,
    const float* __restrict__ rand,
    int len) {
  const int index = blockIdx.x * blockDim.x + threadIdx.x;
  if(index < len) {
    if (input[index] == 0)
      output[index] = 0;
    else {
      int exp;
      float prob = abs(frexpf(input[index], &exp)) / 0.5 - 1.; // [0.5, 1) -> [0, 1)
      if (rand[index] >= prob) exp -= 1;
      exp += 127;
      uint8_t encode;
      if (exp<=17) encode = 0;
      else if (exp<=143) encode = uint8_t(exp-17);
      else encode = 127;
      if (input[index] < 0) encode += 128;
      output[index] = encode;
    }
  }
}


void cnat_compress(float* input, uint8_t* output, int count, cudaStream_t stream, curandGenerator_t* gen) {
    const int threads = 1024;
    auto blocks = count/threads;
    if (count%threads || !blocks) blocks++;
    float *rand;
    cudaMalloc((void **)&rand, count*sizeof(float)); // (0, 1]
    curandGenerateUniform(*gen, rand, count);
    cnat_compress_cuda_kernel<<<blocks, threads, 0, stream>>>(
            input,
            output,
            rand,
            count);
}

__global__ void cnat_decompress_cuda_kernel(
    uint8_t* __restrict__ input,
    float* __restrict__ output,
    int len) {
  const int index = blockIdx.x * blockDim.x + threadIdx.x;
  if(index < len) {
      uint32_t decode;
      if (input[index]<=127) decode = input[index]+17;
      else decode = input[index]+145;
      if (!input[index] % 128) decode -= 17;
      uint32_t sign_and_exp = decode << 23;
      output[index] = reinterpret_cast<float &>(sign_and_exp);
  }
}

void cnat_decompress(uint8_t* input, float* output, int count, cudaStream_t stream) {
  const int threads = 1024;
  auto blocks = count/threads;
  if (count%threads || !blocks) blocks++;
  cnat_decompress_cuda_kernel<<<blocks, threads, 0, stream>>>(
    input,
    output,
    count);
}
#endif
