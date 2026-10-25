// Standalone launcher diagnostic. No emulator, game, or payment ledger is opened.
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "QR/qrcodegen.hpp"
#include "sats_arena_local.h"

int main(int argc, char** argv)
{
    if (argc > 2 || (argc == 2 && std::string(argv[1]) != "--invoice")) return 2;
    std::string input;
    char ch;
    while (std::cin.get(ch) && ch != '\n') {
        if (input.size() >= 16384) return 2;
        input += ch;
    }
    char output[8192]{};
    if (!arena_wallet_check(input.c_str(), argc == 2, output, sizeof(output))) return 1;
    try {
        auto result = nlohmann::json::parse(output);
        if (result.value("ok", false) && result.count("invoice")) {
            const auto invoice = result["invoice"].get<std::string>();
            const auto qr = qrcodegen::QrCode::encodeText(invoice.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
            const int size = qr.getSize() + 8;
            std::string svg = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 " + std::to_string(size) + " " + std::to_string(size) + "' shape-rendering='crispEdges'><path fill='white' d='M0 0h" + std::to_string(size) + "v" + std::to_string(size) + "H0z'/><path fill='black' d='";
            for (int y = 0; y < qr.getSize(); ++y)
                for (int x = 0; x < qr.getSize(); ++x)
                    if (qr.getModule(x, y)) svg += "M" + std::to_string(x + 4) + " " + std::to_string(y + 4) + "h1v1h-1z";
            result["qr_svg"] = svg + "'/></svg>";
        }
        std::cout << result.dump() << std::endl;
        return 0;
    } catch (...) {
        std::cout << "{\"ok\":false,\"message\":\"Wallet check returned an invalid result\"}" << std::endl;
        return 1;
    }
}
