// Native SDK reference for cross-platform codec compatibility tests.
#include "oodle2.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>
int main(int argc, char** argv) {
    try {
        if (argc != 5) throw std::runtime_error("Usage: oodle-native compress|decompress input output rawBytes");
        std::ifstream file(argv[2], std::ios::binary);
        if (!file) throw std::runtime_error("Cannot open input");
        std::vector<char> input((std::istreambuf_iterator<char>(file)), {});
        const bool encode = std::string(argv[1]) == "compress";
        const auto size = encode ? OodleLZ_GetCompressedBufferSizeNeeded(OodleLZ_Compressor_Kraken, input.size()) : std::stoll(argv[4]);
        std::vector<char> output(static_cast<size_t>(size));
        const auto count = encode
            ? OodleLZ_Compress(OodleLZ_Compressor_Kraken, input.data(), input.size(), output.data(), OodleLZ_CompressionLevel_Normal)
            : OodleLZ_Decompress(input.data(), input.size(), output.data(), size, OodleLZ_FuzzSafe_Yes, OodleLZ_CheckCRC_Yes, OodleLZ_Verbosity_None);
        if (count <= 0 || count > size) throw std::runtime_error("Codec failed");
        std::ofstream result(argv[3], std::ios::binary);
        result.write(output.data(), count);
        if (!result) throw std::runtime_error("Cannot write output");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what(); return 1; }
}
