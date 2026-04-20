#include "world/locations_loader.h"

#include <yaml-cpp/yaml.h>

#include <sstream>
#include <unordered_set>

namespace game::world {

namespace {

LocationGraph parse(const YAML::Node& root) {
    if (!root || !root.IsMap()) {
        throw LocationLoadError("locations: top-level node must be a map");
    }

    auto spawn_node = root["spawn_location_id"];
    if (!spawn_node) {
        throw LocationLoadError("locations: missing spawn_location_id");
    }
    LocationGraph g;
    g.spawn_id = spawn_node.as<LocationId>();

    auto locs_node = root["locations"];
    if (!locs_node || !locs_node.IsSequence()) {
        throw LocationLoadError("locations: missing or non-sequence locations[]");
    }

    for (const auto& node : locs_node) {
        if (!node.IsMap()) {
            throw LocationLoadError("locations: each location must be a map");
        }
        Location loc;
        loc.id = node["id"].as<LocationId>();
        loc.name = node["name"] ? node["name"].as<std::string>() : std::string{};
        loc.description = node["description"] ? node["description"].as<std::string>() : std::string{};

        if (auto links_node = node["links"]; links_node) {
            if (!links_node.IsSequence()) {
                std::ostringstream oss;
                oss << "locations: location id=" << loc.id << " has non-sequence links";
                throw LocationLoadError(oss.str());
            }
            for (const auto& link_node : links_node) {
                LocationLink link;
                link.to = link_node["to"].as<LocationId>();
                link.travel_ticks = link_node["travel_ticks"].as<uint16_t>();
                if (link.travel_ticks == 0) {
                    std::ostringstream oss;
                    oss << "locations: location id=" << loc.id
                        << " link to=" << link.to << " has travel_ticks=0";
                    throw LocationLoadError(oss.str());
                }
                loc.links.push_back(link);
            }
        }

        if (g.locations.contains(loc.id)) {
            std::ostringstream oss;
            oss << "locations: duplicate location id=" << loc.id;
            throw LocationLoadError(oss.str());
        }
        g.locations.emplace(loc.id, std::move(loc));
    }

    if (!g.locations.contains(g.spawn_id)) {
        std::ostringstream oss;
        oss << "locations: spawn_location_id=" << g.spawn_id << " refers to unknown location";
        throw LocationLoadError(oss.str());
    }

    // Validate every link target exists, and every link has a reverse with
    // matching travel_ticks (we don't require equal cost, just existence).
    for (const auto& [id, loc] : g.locations) {
        for (const auto& link : loc.links) {
            auto it = g.locations.find(link.to);
            if (it == g.locations.end()) {
                std::ostringstream oss;
                oss << "locations: location id=" << id
                    << " links to unknown id=" << link.to;
                throw LocationLoadError(oss.str());
            }
            bool reverse_found = false;
            for (const auto& rev : it->second.links) {
                if (rev.to == id) { reverse_found = true; break; }
            }
            if (!reverse_found) {
                std::ostringstream oss;
                oss << "locations: link " << id << " -> " << link.to
                    << " has no reverse link";
                throw LocationLoadError(oss.str());
            }
        }
    }

    return g;
}

} // namespace

LocationGraph load_locations(const std::string& path) {
    try {
        YAML::Node root = YAML::LoadFile(path);
        return parse(root);
    } catch (const LocationLoadError&) {
        throw;
    } catch (const YAML::Exception& e) {
        throw LocationLoadError(std::string("locations: YAML parse error: ") + e.what());
    }
}

LocationGraph load_locations_from_string(const std::string& yaml) {
    try {
        YAML::Node root = YAML::Load(yaml);
        return parse(root);
    } catch (const LocationLoadError&) {
        throw;
    } catch (const YAML::Exception& e) {
        throw LocationLoadError(std::string("locations: YAML parse error: ") + e.what());
    }
}

} // namespace game::world
