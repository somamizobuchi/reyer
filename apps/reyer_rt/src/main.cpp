#include <cstdlib>
#include <print>
#include "reyer_rt/app.hpp"

int main(int argc, char **argv) {
    reyer_rt::App app(argc, argv);

    try {
        app.Launch();
    } catch(std::exception &e) {
        std::println("Uncaught exception: {}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
