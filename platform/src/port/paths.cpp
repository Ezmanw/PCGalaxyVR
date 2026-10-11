// The PC's folders for the game (see port/paths.h).
#include "port/paths.h"

#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

const char kAppDir[] = "PCGalaxyVR";

// $VAR when set to an absolute path, else $HOME/fallback.
std::string xdgDir(const char* var, const char* fallback) {
    const char* v = getenv(var);
    std::string base = (v && v[0] == '/') ? std::string(v) : port_home_dir() + "/" + fallback;
    std::string dir = base + "/" + kAppDir;
    port_make_dirs(dir);
    return dir;
}

}  // namespace

std::string port_home_dir() {
    const char* home = getenv("HOME");
    if (home && home[0] == '/') {
        return home;
    }
    if (struct passwd* pw = getpwuid(getuid())) {
        if (pw->pw_dir) return pw->pw_dir;
    }
    return "/tmp";
}

std::string port_data_dir() { return xdgDir("XDG_DATA_HOME", ".local/share"); }
std::string port_config_dir() { return xdgDir("XDG_CONFIG_HOME", ".config"); }
std::string port_cache_dir() { return xdgDir("XDG_CACHE_HOME", ".cache"); }

bool port_make_dirs(const std::string& path) {
    for (size_t i = 1; i <= path.size(); i++) {
        if (i == path.size() || path[i] == '/') {
            std::string part = path.substr(0, i);
            if (mkdir(part.c_str(), 0755) != 0 && errno != EEXIST) {
                return false;
            }
        }
    }
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
