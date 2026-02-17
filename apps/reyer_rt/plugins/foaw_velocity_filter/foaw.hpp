#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

class FOAW {
  public:
    /**
     * @param maxWindowSize Maximum number of samples to look back.
     * @param samplingTime (T) Time between samples in seconds.
     * @param noiseBound (delta) The maximum allowed deviation from a linear
     * fit.
     */
    explicit FOAW(int maxWindowSize, double samplingTime, double noiseBound)
        : max_n(maxWindowSize), T(samplingTime), delta(noiseBound), count(0),
          head(0) {
        history.resize(max_n);
    }

    ~FOAW() = default;

    /**
     * Process a new position sample and return the estimated velocity.
     */
    double update(double position) {
        // Store new sample in circular buffer
        history[head] = position;

        // Update window tracking
        if (count < max_n)
            count++;

        int best_k = 1; // Default to simplest finite difference (window of 1)

        // Iterate through possible window sizes (k) to find the largest valid
        // one Per FOAW logic: slope = (yk - y0) / (k * T) All intermediate
        // points i must satisfy |yi - (y0 + slope * i * T)| <= delta
        for (int k = 2; k < count; ++k) {
            double y_curr = history[head];
            double y_k = get_sample(k);
            double slope = (y_curr - y_k) / (k * T);

            bool valid = true;
            for (int i = 1; i < k; ++i) {
                double expected = y_k + (slope * i * T);
                double actual = get_sample(k - i);
                if (std::abs(actual - expected) > delta) {
                    valid = false;
                    break;
                }
            }

            if (valid) {
                best_k = k;
            } else {
                // If this window is invalid, larger windows are generally
                // ignored
                break;
            }
        }

        // Calculate final velocity based on the best (widest) window found
        double velocity = (history[head] - get_sample(best_k)) / (best_k * T);

        // Move head for next sample
        head = (head + 1) % max_n;

        return velocity;
    }

  private:
    // Helper to get sample at 'offset' steps in the past
    double get_sample(int offset) const {
        int index = (head - offset + max_n) % max_n;
        return history[index];
    }

    int max_n;
    double T;
    double delta;
    int count;
    int head;
    std::vector<double> history;
};