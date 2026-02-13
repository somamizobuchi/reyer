#include <cmath>
#include <vector>

#include <raylib.h>
#include <cuda_runtime.h>
#include <cufft.h>

#include "butterworth_filter.h"
#include "cuda_gl_interop.h"

#include "reyer/plugin/interfaces.hpp"
#include "reyer/plugin/loader.hpp"

namespace reyer::plugin {

struct SaccadeContingentConfig {
    std::string image_path = "mona_lisa.png";
    int image_width = 1374;
    int image_height = 1374;
    float speed_divisor = 150.0f;
    int butterworth_order = 2;
    float initial_cutoff = 1.0f;
};

class SaccadeContingent : public RenderPluginBase<SaccadeContingentConfig> {
  public:
    SaccadeContingent() = default;
    ~SaccadeContingent() = default;

  protected:
    void onInit() override {
        std::cout << "Initializing Saccade Contingent Plugin..." << std::endl;
        cudaSetDevice(0);

        const auto &config = getConfig();
        butterworth_order_ = config.butterworth_order;
        cutoff_frequency_ = config.initial_cutoff;
        speed_divisor_ = config.speed_divisor;

        // Load and convert image to grayscale float
        Image img = LoadImage(config.image_path.c_str());
        if (img.width != config.image_width || img.height != config.image_height) {
            ImageCrop(&img, {0, 0, (float)config.image_width, (float)config.image_height});
        }
        img_width_ = img.width;
        img_height_ = img.height;

        ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
        auto *pixels = static_cast<unsigned char *>(img.data);
        int N = img_width_ * img_height_;
        std::vector<float> input_data(N);
        for (int i = 0; i < N; i++) {
            float gray = 0.299f * pixels[i * 3] + 0.587f * pixels[i * 3 + 1] +
                         0.114f * pixels[i * 3 + 2];
            input_data[i] = gray / 255.0f;
        }
        UnloadImage(img);

        // Allocate GPU buffers
        cudaMalloc(&d_input_complex_, N * sizeof(cufftComplex));
        cudaMalloc(&d_freq_domain_, N * sizeof(cufftComplex));
        cudaMalloc(&d_freq_shifted_, N * sizeof(cufftComplex));
        cudaMalloc(&d_filtered_, N * sizeof(cufftComplex));

        // Create cuFFT plans
        cufftPlan2d(&plan_forward_, img_height_, img_width_, CUFFT_C2C);
        cufftPlan2d(&plan_inverse_, img_height_, img_width_, CUFFT_C2C);

        // Precompute forward FFT (result stays in d_freq_shifted_ for reuse)
        float *d_input_real;
        cudaMalloc(&d_input_real, N * sizeof(float));
        cudaMemcpy(d_input_real, input_data.data(), N * sizeof(float),
                   cudaMemcpyHostToDevice);

        butterworth_real_to_complex(d_input_real, d_input_complex_, N, 0);
        cudaDeviceSynchronize();
        cufftExecC2C(plan_forward_, d_input_complex_, d_freq_domain_, CUFFT_FORWARD);
        butterworth_fft_shift(d_freq_domain_, d_freq_shifted_, img_width_, img_height_, 0);
        cudaDeviceSynchronize();

        cudaFree(d_input_real);

        // Create texture for filtered output
        Image placeholder = GenImageColor(img_width_, img_height_, BLACK);
        filtered_texture_ = LoadTextureFromImage(placeholder);
        UnloadImage(placeholder);

        // Register with CUDA-GL interop
        gl_interop_.registerTexture(filtered_texture_.id);

        // Apply initial filter
        applyFilter();
    }

    void onShutdown() override {
        gl_interop_.unregisterTexture();
        UnloadTexture(filtered_texture_);

        cudaFree(d_input_complex_);
        cudaFree(d_freq_domain_);
        cudaFree(d_freq_shifted_);
        cudaFree(d_filtered_);
        cufftDestroy(plan_forward_);
        cufftDestroy(plan_inverse_);
    }

    void onPause() override {}
    void onResume() override {}

    void onReset() override {
        const auto &config = getConfig();
        cutoff_frequency_ = config.initial_cutoff;
        butterworth_order_ = config.butterworth_order;
        speed_divisor_ = config.speed_divisor;
        need_update_ = true;
    }

    void onConsume(const core::EyeData &data) override {
        // const auto &eye = data.right.is_valid ? data.right : data.left;
        // if (!eye.is_valid)
        //     return;

        const auto &eye = data.left; // Use left eye for saccade detection
        float vx = eye.gaze.velocity.x;
        float vy = eye.gaze.velocity.y;
        float speed = std::sqrt(vx * vx + vy * vy);

        cutoff_frequency_ = 1.0f / (1.0f + speed / speed_divisor_);
        need_update_ = true;
    }

    void onRender() override {
        if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_TRIGGER_1)) {
            speed_divisor_ = fmax(10.0f, speed_divisor_ - 10.0f);
        }
        if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) {
            speed_divisor_ += 10.0f;
        }

        if (need_update_) {
            applyFilter();
            need_update_ = false;
        }

        DrawRectangle(0, 0, GetScreenWidth() * speed_divisor_ / 300.0f, 100, BLACK);
        // const char *text = TextFormat("Speed divisor: %.0f", speed_divisor_);
        // DrawText(text, 10, 60, 20, WHITE);

        int window_width = GetScreenWidth();
        int window_height = GetScreenHeight();

        float scale_x = (float)window_width / (float)img_width_;
        float scale_y = (float)window_height / (float)img_height_;
        float scale = fmax(scale_x, scale_y);

        int scaled_width = (int)(img_width_ * scale);
        int scaled_height = (int)(img_height_ * scale);
        int pos_x = (window_width - scaled_width) / 2;
        int pos_y = (window_height - scaled_height) / 2;

        Rectangle source = {0.0f, 0.0f, (float)img_width_, (float)img_height_};
        Rectangle dest = {(float)pos_x, (float)pos_y, (float)scaled_width,
                          (float)scaled_height};
        DrawTexturePro(filtered_texture_, source, dest, {0.0f, 0.0f}, 0.0f, WHITE);
    }

  private:
    void applyFilter() {
        cudaSurfaceObject_t surf = gl_interop_.mapTexture();

        butterworth_lowpass(d_freq_shifted_, d_filtered_, img_width_, img_height_,
                            cutoff_frequency_, butterworth_order_, 0);
        cudaDeviceSynchronize();

        butterworth_fft_shift(d_filtered_, d_input_complex_, img_width_, img_height_, 0);
        cudaDeviceSynchronize();

        cufftExecC2C(plan_inverse_, d_input_complex_, d_freq_domain_, CUFFT_INVERSE);
        cudaDeviceSynchronize();

        float norm_factor = 1.0f / (img_width_ * img_height_);
        butterworth_magnitude_to_texture(d_freq_domain_, surf, img_width_, img_height_,
                                         norm_factor, 0);
        cudaDeviceSynchronize();

        gl_interop_.unmapTexture();
    }

    int img_width_ = 0;
    int img_height_ = 0;

    float speed_divisor_ = 150.0f;
    float cutoff_frequency_ = 1.0f;
    int butterworth_order_ = 2;
    bool need_update_ = true;

    cufftComplex *d_input_complex_ = nullptr;
    cufftComplex *d_freq_domain_ = nullptr;
    cufftComplex *d_freq_shifted_ = nullptr;
    cufftComplex *d_filtered_ = nullptr;
    cufftHandle plan_forward_ = 0;
    cufftHandle plan_inverse_ = 0;

    Texture2D filtered_texture_ = {};
    CudaGLInterop gl_interop_;
};

} // namespace reyer::plugin

REYER_PLUGIN_ENTRY(reyer::plugin::SaccadeContingent, "Saccade Contingent", 1)
