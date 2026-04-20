#include "world/locations_loader.h"

#include <gtest/gtest.h>

#include <string>

namespace {

const char* kGoodGraph = R"(
spawn_location_id: 1
locations:
  - id: 1
    name: Town
    description: A small quiet town square.
    links:
      - to: 2
        travel_ticks: 8
  - id: 2
    name: Forest
    description: A dense forest.
    links:
      - to: 1
        travel_ticks: 8
      - to: 3
        travel_ticks: 12
  - id: 3
    name: Clearing
    description: A sunlit clearing.
    links:
      - to: 2
        travel_ticks: 12
)";

}  // namespace

TEST(LocationsLoader, AcceptsValidGraph) {
    auto graph = game::world::load_locations_from_string(kGoodGraph);
    EXPECT_EQ(graph.spawn_id, 1u);
    EXPECT_EQ(graph.locations.size(), 3u);

    const auto& town = graph.locations.at(1);
    EXPECT_EQ(town.name, "Town");
    ASSERT_EQ(town.links.size(), 1u);
    EXPECT_EQ(town.links[0].to, 2u);
    EXPECT_EQ(town.links[0].travel_ticks, 8u);

    const auto& forest = graph.locations.at(2);
    EXPECT_EQ(forest.links.size(), 2u);
}

TEST(LocationsLoader, RejectsMissingSpawnLocation) {
    const std::string yaml = R"(
spawn_location_id: 99
locations:
  - id: 1
    name: Town
    description: x
    links: []
)";
    EXPECT_THROW(game::world::load_locations_from_string(yaml),
                 game::world::LocationLoadError);
}

TEST(LocationsLoader, RejectsAsymmetricLink) {
    // 1 → 2 exists but 2 → 1 does not.
    const std::string yaml = R"(
spawn_location_id: 1
locations:
  - id: 1
    name: A
    description: x
    links:
      - to: 2
        travel_ticks: 4
  - id: 2
    name: B
    description: x
    links: []
)";
    EXPECT_THROW(game::world::load_locations_from_string(yaml),
                 game::world::LocationLoadError);
}

TEST(LocationsLoader, RejectsLinkToUnknownLocation) {
    const std::string yaml = R"(
spawn_location_id: 1
locations:
  - id: 1
    name: A
    description: x
    links:
      - to: 99
        travel_ticks: 4
)";
    EXPECT_THROW(game::world::load_locations_from_string(yaml),
                 game::world::LocationLoadError);
}

TEST(LocationsLoader, RejectsDuplicateLocationId) {
    const std::string yaml = R"(
spawn_location_id: 1
locations:
  - id: 1
    name: A
    description: x
    links: []
  - id: 1
    name: B
    description: x
    links: []
)";
    EXPECT_THROW(game::world::load_locations_from_string(yaml),
                 game::world::LocationLoadError);
}

TEST(LocationsLoader, RejectsZeroTravelTicks) {
    const std::string yaml = R"(
spawn_location_id: 1
locations:
  - id: 1
    name: A
    description: x
    links:
      - to: 2
        travel_ticks: 0
  - id: 2
    name: B
    description: x
    links:
      - to: 1
        travel_ticks: 0
)";
    EXPECT_THROW(game::world::load_locations_from_string(yaml),
                 game::world::LocationLoadError);
}

TEST(LocationsLoader, EmptyGraphIsRejected) {
    const std::string yaml = R"(
spawn_location_id: 1
locations: []
)";
    EXPECT_THROW(game::world::load_locations_from_string(yaml),
                 game::world::LocationLoadError);
}
