#pragma once

#include "core/Args.hpp"

namespace pinball {

class Game;

// Sets up the scene on a game that has just been made. Returns false, with
// the game in some state of play, if the table did not do what the scene
// needs (only a change to the table or the rules could make it so).
bool stageScene(Game& game, Scene scene);

}  // namespace pinball
