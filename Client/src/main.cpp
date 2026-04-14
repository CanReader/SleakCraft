#include <Core/Application.hpp>
#include <Core/CommandLine.hpp>
#include <Game.hpp>
#include <Logger.hpp>
#include <filesystem>
#include <iostream>

#define PROJECT_NAME "SleakCraft"

static void PrintHelp(const char* exe) {
    std::cout
        << "\nUsage: " << exe << " [OPTIONS]\n"
        << "\nRenderer\n"
        << "  -r vulkan          Use Vulkan\n"
        << "  -r d3d11           Use DirectX 11 (default on Windows)\n"
        << "  -r d3d12           Use DirectX 12\n"
        << "  -r opengl          Use OpenGL\n"
        << "\nWindow\n"
        << "  -w <pixels>        Window width        (default: 1200)\n"
        << "  -h <pixels>        Window height       (default: 800)\n"
        << "  -t <name>          Window title        (use _ for spaces)\n"
        << "  --fullscreen       Start in fullscreen\n"
        << "\nWorld\n"
        << "  -world <name>      Load world if save exists, create new otherwise\n"
        << "  -seed <n>          Seed for new world (default: random)\n"
        << "  -rd <n>            Initial render distance in chunks (default: 8)\n"
        << "\nGraphics\n"
        << "  -msaa <n>          MSAA sample count: 1, 2, 4, 8\n"
        << "  --vsync            Enable VSync on launch\n"
        << "  --no-vsync         Disable VSync on launch\n"
        << "\nBenchmark\n"
        << "  --bench            Start benchmark recording immediately\n"
        << "\nDebug\n"
        << "  --validate         Enable Vulkan validation layer\n"
        << "\nMisc\n"
        << "  --help             Show this message\n\n";
}

int main(int argc, char** argv) {
    // Set working directory to executable location so relative asset paths resolve correctly
    std::filesystem::current_path(
        std::filesystem::path(argv[0]).parent_path().empty()
            ? std::filesystem::current_path()
            : std::filesystem::weakly_canonical(
                  std::filesystem::path(argv[0])).parent_path());

    // Register game's help text, then parse CLI arguments
    Sleak::CommandLine::SetHelpCallback(PrintHelp);
    Sleak::CommandLine::Parse(argc, argv);

    Sleak::Logger::Init((char*)PROJECT_NAME);

    Sleak::ApplicationDefaults defaults {
        .Name             = PROJECT_NAME,
        .CommandLineArgs  = Sleak::Arguments(argc, argv),
    };

    Game* game = new Game();
    Sleak::Application app(defaults);

    return app.Run(game);
}
