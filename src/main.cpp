#include <iostream>
#include <asio.hpp>

int main() {
    try {
        std::cout << "FluxGate starting..." << '\n';
        asio::io_context io_context;
        std::cout << "FluxGate is ready to intercept traffic" << '\n';
        io_context.run();
    } catch (std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << '\n';
    }
    return 0;
}
