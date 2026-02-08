#include "ap.h"
#include "interpolation.h"
#include <vector>
#include <cstdio>
#include <reyer/core/vec2.hpp>
#include <reyer/plugin/interfaces.hpp>

namespace reyer::plugin::calibration {

struct RBFCoefficients {
    int output_dim;           // Which output (0 = x, 1 = y)
    std::vector<double> rbf_weights;  // Weights for each basis function
    double linear_x;          // Linear coefficient for x
    double linear_y;          // Linear coefficient for y
    double constant;          // Constant term
};

struct RBFModelCoefficients {
    std::vector<RBFCoefficients> outputs;  // One entry per output dimension
    std::vector<vec2<float>> centers;            // RBF basis function centers
    int num_centers;
};

class Calibration {
  public:
    Calibration() = default;
    ~Calibration() = default;

    void Init() {
        alglib::rbfcreate(2, 2, model_);
    }

    vec2<float> calibrate(vec2<float> &p) {
        try {
            alglib::real_1d_array control_points;
            control_points.setlength(2);
            control_points[0] = static_cast<double>(p.x);
            control_points[1] = static_cast<double>(p.y);

            alglib::real_1d_array result;
            alglib::rbfcalc(model_, control_points, result);
            return {static_cast<float>(result[0]),
                    static_cast<float>(result[1])};
        } catch (alglib::ap_error &err) {
            std::printf("RBF Calibration Error: %s\n", err.msg.c_str());
        }
        return {};
    }

    void setPoints(std::span<const CalibrationPoint> points) {
        try {
            is_calibrated_ = false;
            if (points.empty()) return;

            // Reinitialize model to clear any previous state
            alglib::rbfcreate(2, 2, model_);

            alglib::real_2d_array data;
            data.setlength(points.size(), 4);  // [xc, yc, xm, ym]

            for (int i = 0; i < points.size(); i++) {
                data[i][0] = static_cast<double>(points[i].measured_point.x);  // input: control.x
                data[i][1] = static_cast<double>(points[i].measured_point.y);  // input: control.y
                data[i][2] = static_cast<double>(points[i].control_point.x);  // output: measured.x
                data[i][3] = static_cast<double>(points[i].control_point.y);  // output: measured.y
            }

            double lambda = 0.01;  // adjust based on your needs

            alglib::rbfsetpoints(model_, data);
            alglib::rbfsetalgothinplatespline(model_, lambda);
            alglib::rbfsetlinterm(model_);

            alglib::rbfreport report;
            alglib::rbfbuildmodel(model_, report);
            std::printf("RBF Build Report - RMSError: %f, MaxError: %f\n",
                        report.rmserror, report.maxerror);
            
            is_calibrated_ = true;
        } catch (alglib::ap_error &err) {
            std::printf("RBF Error: %s\n", err.msg.c_str());
        }
    }

    RBFModelCoefficients getCoefficients() {
        RBFModelCoefficients result;
        try {
            alglib::ae_int_t nx, ny, nc, modelversion;
            alglib::real_2d_array xwr;  // Centers and weights
            alglib::real_2d_array v;    // Polynomial term

            alglib::rbfunpack(model_, nx, ny, xwr, nc, v, modelversion);

            result.num_centers = nc;

            // Extract centers
            for (int i = 0; i < nc; i++) {
                result.centers.emplace_back(static_cast<float>(xwr[i][0]),
                                            static_cast<float>(xwr[i][1]));
            }

            // Extract coefficients for each output dimension
            for (int out_dim = 0; out_dim < ny; out_dim++) {
                RBFCoefficients coeff;
                coeff.output_dim = out_dim;

                // Extract linear coefficients and constant from v matrix
                // v is array[NY, NX+1]
                // First NX elements are linear, last element is constant
                coeff.linear_x = v[out_dim][0];
                coeff.linear_y = v[out_dim][1];
                coeff.constant = v[out_dim][2];  // v[out_dim][NX] = v[out_dim][2]

                // Extract RBF weights from xwr matrix
                // xwr is array[NC, NX+1]
                // Last column contains the weights
                for (int i = 0; i < nc; i++) {
                    coeff.rbf_weights.push_back(xwr[i][nx]);  // nx=2, so column 2
                }

                result.outputs.push_back(coeff);
            }

        } catch (alglib::ap_error &err) {
            std::printf("Error extracting coefficients: %s\n", err.msg.c_str());
        }
        return result;
    }

    void printCoefficients() {
        RBFModelCoefficients coeffs = getCoefficients();

        std::printf("\n=== RBF Model Coefficients ===\n");
        std::printf("Number of centers: %d\n\n", coeffs.num_centers);

        std::printf("Basis Function Centers:\n");
        for (int i = 0; i < coeffs.centers.size(); i++) {
            std::printf("  Center %d: (%.4f, %.4f)\n", i, coeffs.centers[i].x,
                       coeffs.centers[i].y);
        }

        for (const auto& output : coeffs.outputs) {
            std::printf("\nOutput Dimension %d (Measured.%s):\n", output.output_dim,
                       output.output_dim == 0 ? "x" : "y");
            std::printf("  Polynomial: %.6f*x + %.6f*y + %.6f\n",
                       output.linear_x, output.linear_y, output.constant);
            std::printf("  RBF Weights:\n");
            for (int i = 0; i < output.rbf_weights.size(); i++) {
                std::printf("    w[%d] = %.6f\n", i, output.rbf_weights[i]);
            }
        }
        std::printf("\n");
    }

    bool isCalibrated() {
        return is_calibrated_;
    }

  private:
    alglib::rbfmodel model_;
    bool is_calibrated_ = false;
};
}