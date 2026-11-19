#pragma once

#include "app/App.hpp"

namespace horde::app {

enum class ParseResult {
    Run,   // start the game with the parsed config
    Exit,  // the arguments were fully handled (--help, --list-physics)
    Error, // bad arguments; a message has been printed
};

// Fills `config` from argv. See printUsage in CommandLine.cpp for the flags.
ParseResult parseCommandLine(int argc, char* argv[], AppConfig& config);

} // namespace horde::app
