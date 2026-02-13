#ifndef BUTTERWORTH_FILTER_H
#define BUTTERWORTH_FILTER_H

#include <cuda_runtime.h>
#include <cufft.h>

#ifdef __cplusplus
extern "C" {
#endif

// Convert real float array to cufftComplex (imaginary part = 0).
// All pointers are device memory.
void butterworth_real_to_complex(const float *input, cufftComplex *output,
                                 int size, cudaStream_t stream);

// FFT shift: move zero-frequency component to center of spectrum.
// All pointers are device memory.
void butterworth_fft_shift(cufftComplex *input, cufftComplex *output,
                           int width, int height, cudaStream_t stream);

// Apply Butterworth low-pass filter in frequency domain.
// Expects centered spectrum (after fft_shift). All pointers are device memory.
void butterworth_lowpass(cufftComplex *input, cufftComplex *output,
                         int width, int height,
                         float cutoff_freq, int order, cudaStream_t stream);

// Compute magnitude from complex frequency data and write as RGBA grayscale
// to a CUDA surface object (for GL-CUDA interop).
// freq_domain is device memory.
void butterworth_magnitude_to_texture(cufftComplex *freq_domain,
                                      cudaSurfaceObject_t surf,
                                      int width, int height,
                                      float norm_factor, cudaStream_t stream);

#ifdef __cplusplus
}
#endif

#endif // BUTTERWORTH_FILTER_H
