#pragma once

#include <yaml-cpp/yaml.h>

#include <cstdint>
#include <string>
#include <stdexcept>

namespace game::config {

struct Config {
    // greeting
    std::string greeting_message = "Hello, world!";

    // logging
    std::string log_level = "info";
    std::string log_file = "logs/game-server.log";

    static Config load(const std::string& path) {
        Config cfg;
        YAML::Node root = YAML::LoadFile(path);

        if (auto greeting = root["greeting"]; greeting && greeting.IsMap()) {
            if (greeting["message"]) cfg.greeting_message = greeting["message"].as<std::string>(cfg.greeting_message);
        }

        if (auto logging = root["logging"]; logging && logging.IsMap()) {
            if (logging["level"]) cfg.log_level = logging["level"].as<std::string>(cfg.log_level);
            if (logging["file"])  cfg.log_file  = logging["file"].as<std::string>(cfg.log_file);
        }

        return cfg;
    }
};

inline Config& get() {
    static Config instance;
    return instance;
}

inline void load(const std::string& path) {
    get() = Config::load(path);
}

} // namespace game::config
