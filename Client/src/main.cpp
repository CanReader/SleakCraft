#include <iostream>
#include <Core/Application.hpp>
#include <Game.hpp>
#include <string>
#include <Logger.hpp>
#include <filesystem>

#define PROJECT_NAME "SleakCraft"

std::string HelpMessage =
  "Usage: SleakCraft [OPTION...] \n\
  help : Shows this help message \n\
  -w :    Sets width of the window. \n\
  -h :    Sets height of the window \n\
  -t :    Sets title of the window, to set space between words put _ character \n\
  ";

int main(int argc, char** argv) {
  // Set working directory to executable location so relative asset paths resolve correctly
  std::filesystem::current_path(std::filesystem::path(argv[0]).parent_path().empty()
      ? std::filesystem::current_path()
      : std::filesystem::weakly_canonical(std::filesystem::path(argv[0])).parent_path());

  Sleak::Logger::Init((char*)PROJECT_NAME);

  Sleak::ApplicationDefaults defaults
  {
    .Name = PROJECT_NAME,
    .CommandLineArgs = Sleak::Arguments(argc, argv)
  };

  Game* game = new Game();
  Sleak::Application app(defaults);

  return app.Run( game );
}
