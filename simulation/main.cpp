// Include standard output stream for printing the Core version.
#include <iostream>

// Include the Core placeholder header that declares core_version().
#include "Types.hpp"

// Simulation entry point for Milestone 1.
int main()
{
    // Query the Core library version to verify linkage.
    const int version = core_version();

    // Print the Core version so the run result is visible.
    std::cout << "core_version=" << version << std::endl;

    // Exit successfully as required by Milestone 1.
    return 0;
}
