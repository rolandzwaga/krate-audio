#include "cli.h"
#include "main.h"

#include <cstdio>
#include <exception>

int main(int argc, char** argv) {
    try {
        MembrumFit::CliArgs args;
        const int cliRc = MembrumFit::parseCli(argc, argv, args);
        if (cliRc != 0) {
            return cliRc;
        }
        return MembrumFit::runMembrumFit(args);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "membrum-fit: fatal: %s\n", e.what());
        return 2;
    } catch (...) {
        std::fprintf(stderr, "membrum-fit: fatal: unknown exception\n");
        return 2;
    }
}
