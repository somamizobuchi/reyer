#include "butterworth_filter.h"

// --- Kernels (duplicated from butterworth_filter.cu to keep the library self-contained) ---

static __global__ void realToComplexKernel(const float *input, cufftComplex *output, int size)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < size)
    {
        output[idx].x = input[idx];
        output[idx].y = 0.0f;
    }
}

static __global__ void fftShiftKernel(cufftComplex *input, cufftComplex *output, int width, int height)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x < width && y < height)
    {
        int src_idx = y * width + x;
        int shifted_x = (x + width / 2) % width;
        int shifted_y = (y + height / 2) % height;
        int dst_idx = shifted_y * width + shifted_x;
        output[dst_idx] = input[src_idx];
    }
}

static __global__ void butterworthLowPassKernel(cufftComplex *input, cufftComplex *output,
                                                int width, int height, float cutoff_freq, int order)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x < width && y < height)
    {
        int idx = y * width + x;

        float fx = ((float)x - width / 2.0f) / width;
        float fy = ((float)y - height / 2.0f) / height;
        float freq_dist = sqrt(fx * fx + fy * fy);

        float filter_value;
        if (freq_dist < 1e-6f)
        {
            filter_value = 1.0f;
        }
        else
        {
            float ratio = freq_dist / cutoff_freq;
            float ratio_power = pow(ratio, 2.0f * order);
            filter_value = 1.0f / (1.0f + ratio_power);
        }

        output[idx].x = input[idx].x * filter_value;
        output[idx].y = input[idx].y * filter_value;
    }
}

static __global__ void magnitudeToTextureKernel(cufftComplex *freq_domain, cudaSurfaceObject_t surf,
                                                int width, int height, float norm_factor)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x < width && y < height)
    {
        int idx = y * width + x;

        float real = freq_domain[idx].x;
        float imag = freq_domain[idx].y;
        float magnitude = sqrt(real * real + imag * imag) * norm_factor;

        magnitude = fmax(0.0f, fmin(1.0f, magnitude));

        unsigned char gray_val = (unsigned char)(magnitude * 255.0f);
        uchar4 rgba = make_uchar4(gray_val, gray_val, gray_val, 255);

        surf2Dwrite(rgba, surf, x * sizeof(uchar4), y);
    }
}

// --- extern "C" launchers ---

extern "C" void butterworth_real_to_complex(const float *input, cufftComplex *output,
                                            int size, cudaStream_t stream)
{
    dim3 block(256);
    dim3 grid((size + block.x - 1) / block.x);
    realToComplexKernel<<<grid, block, 0, stream>>>(input, output, size);
}

extern "C" void butterworth_fft_shift(cufftComplex *input, cufftComplex *output,
                                      int width, int height, cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
    fftShiftKernel<<<grid, block, 0, stream>>>(input, output, width, height);
}

extern "C" void butterworth_lowpass(cufftComplex *input, cufftComplex *output,
                                    int width, int height,
                                    float cutoff_freq, int order, cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
    butterworthLowPassKernel<<<grid, block, 0, stream>>>(input, output, width, height, cutoff_freq, order);
}

extern "C" void butterworth_magnitude_to_texture(cufftComplex *freq_domain,
                                                 cudaSurfaceObject_t surf,
                                                 int width, int height,
                                                 float norm_factor, cudaStream_t stream)
{
    dim3 block(16, 16);
    dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
    magnitudeToTextureKernel<<<grid, block, 0, stream>>>(freq_domain, surf, width, height, norm_factor);
}
