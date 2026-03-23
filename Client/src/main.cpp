#include <Core/Application.hpp>
#include <Core/CommandLine.hpp>
#include <Game.hpp>
#include <Logger.hpp>
#include <filesystem>

#define PROJECT_NAME "SleakCraft"

int main(int argc, char** argv) {
    // Set working directory to executable location so relative asset paths resolve correctly
    std::filesystem::current_path(
        std::filesystem::path(argv[0]).parent_path().empty()
            ? std::filesystem::current_path()
            : std::filesystem::weakly_canonical(
                  std::filesystem::path(argv[0])).parent_path());

    // Parse CLI arguments before anything else — Application and Game read from here
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
