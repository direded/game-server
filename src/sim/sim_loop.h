#pragma once

#include "sim/command.h"

#include <concurrentqueue.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

namespace game::event { class EventDispatcher; }
namespace game::world { class World; }

namespace game::sim {

class ActionResolver;

// The authoritative tick loop. Owns one dedicated thread, the world,
// and an MPSC-friendly command queue.
//
// Lifecycle:
//   SimLoop loop(world, events, 250ms);
//   loop.start();            // spawns thread
//   // ... producers call loop.push_command(...) freely ...
//   loop.stop();              // joins; any queued commands are discarded
//
// Tick order (every tick):
//   1. Drain command queue; execute each Command.
//   2. world.advance() — increment tick counter.
//   3. events.flush() — fan out pending events.
//
// Timing: if one tick runs longer than `tick_period`, the loop does NOT
// compensate on subsequent ticks — it logs a warning and immediately starts
// the next tick. This keeps the sim from "fast-forwarding" bursts of work
// after a stall.
class SimLoop {
public:
    SimLoop(game::world::World& world, game::event::EventDispatcher& events,
            std::chrono::milliseconds tick_period);
    ~SimLoop();

    // Optional resolver run after the command drain and before world.advance().
    // Currently the only resolver — ActionResolver — advances character
    // actions and emits ActionCompleted / CharacterEntered / CharacterLeft
    // events. Set once at construction time; called from the sim thread only.
    void set_action_resolver(ActionResolver* r) { action_resolver_ = r; }

    SimLoop(const SimLoop&) = delete;
    SimLoop& operator=(const SimLoop&) = delete;

    void start();
    void stop();

    // Thread-safe. Callable from any thread (IO threads push commands from
    // packet handlers). Moves the command into the queue; caller loses
    // ownership. Silently dropped after stop() — commands posted during
    // shutdown cannot be replied to.
    void push_command(std::unique_ptr<Command> cmd);

    // Monotonic current tick. Safe to read from any thread.
    uint64_t tick_count() const { return tick_count_.load(std::memory_order_relaxed); }

    // Run N ticks synchronously on the calling thread; intended for tests
    // that need deterministic ordering without racing against a live thread.
    // Must NOT be called while start()/stop() are active.
    void run_one_tick_for_test();

private:
    void run();
    void process_one_tick();

    game::world::World& world_;
    game::event::EventDispatcher& events_;
    std::chrono::milliseconds tick_period_;
    ActionResolver* action_resolver_ = nullptr;

    std::atomic<bool> running_{false};
    std::atomic<uint64_t> tick_count_{0};
    std::thread thread_;

    // moody-camel's ConcurrentQueue is an MPMC lock-free queue — we only need
    // MPSC (many producers / sim thread consumer) but it's the simplest
    // vendored option.
    moodycamel::ConcurrentQueue<std::unique_ptr<Command>> queue_;
};

} // namespace game::sim
