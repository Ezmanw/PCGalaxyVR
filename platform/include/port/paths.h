// The PC's folders for the game (XDG base directories), made on first use.
// Platform code only.
#pragma once

#include <string>

std::string port_home_dir();
std::string port_data_dir();    // $XDG_DATA_HOME/PCGalaxyVR: game files (game/), saves (nand/)
std::string port_config_dir();  // $XDG_CONFIG_HOME/PCGalaxyVR: petari_vr.ini
std::string port_cache_dir();   // $XDG_CACHE_HOME/PCGalaxyVR: shader cache, logs
// mkdir -p; true when the folder exists afterwards.
bool port_make_dirs(const std::string& path);
