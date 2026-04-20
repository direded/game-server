#include "event/dispatcher.h"
#include "event/event.h"
#include "session/session_manager.h"
#include "sim/command.h"
#include "sim/sim_loop.h"
#include "sim/world.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace {

// Minimal test command: increments a shared atomic when executed on the
// sim thread and emits a `Private`-scope event so we can observe
// in-same-tick flushing.
struct CountCommand : public game::sim::Command {
    std::atomic<int>& counter;
    game::net::ConnId target;
    CountCommand(std::atomic<int>& c, game::net::ConnId t) : counter(c), target(t) {}

    void execute(game::sim::CommandContext& ctx) override {
        counter.fetch_add(1, std::memory_order_relaxed);
        game::event::Event ev;
        ev.scope = game::event::EventScope::Private;
        ev.private_target = target;
        ev.packet_id = 0xdead;
        ev.payload = {1, 2, 3, 4};  // raw bytes are fine — tests only count sends
        ctx.events.emit(std::move(ev));
    }
    const char* name() const override { return "Count"; }
};

}  // namespace

// ── The loop advances the tick counter under wall-clock time ──────────────
TEST(SimLoop, AdvancesTickCountOverTime) {
    game::sim::World world;
    game::session::SessionManager sessions;
    game::event::EventDispatcher dispatcher(sessions, [](auto, auto, auto){});
    game::sim::SimLoop loop(world, dispatcher, std::chrono::milliseconds(20));

    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    loop.stop();

    // 20ms period, ran ~250ms — expect at least 5 ticks (generous lower bound
    // so the test isn't flaky on a loaded CI host).
    EXPECT_GE(loop.tick_count(), 5u);
}

// ── A command pushed from thread A is executed on the sim thread ──────────
TEST(SimLoop, CommandEnqueuedFromAnotherThreadExecutes) {
    game::sim::World world;
    game::session::SessionManager sessions;
    game::event::EventDispatcher dispatcher(sessions, [](auto, auto, auto){});
    game::sim::SimLoop loop(world, dispatcher, std::chrono::milliseconds(20));

    std::atomic<int> executed{0};

    loop.start();
    // Push 5 commands in a quick burst from this (test) thread.
    for (int i = 0; i < 5; ++i) {
        loop.push_command(std::make_unique<CountCommand>(executed, /*conn=*/1));
    }
    // Wait long enough for at least one tick to drain the queue.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    loop.stop();

    EXPECT_EQ(executed.load(), 5);
}

// ── Events emitted inside a tick are flushed in the same tick ─────────────
TEST(SimLoop, EmittedEventsFlushInSameTick) {
    game::sim::World world;
    game::session::SessionManager sessions;

    std::atomic<int> sends{0};
    std::atomic<uint32_t> tick_at_send{0};
    game::event::EventDispatcher dispatcher(
        sessions,
        [&](game::net::ConnId, const uint8_t*, size_t) {
            sends.fetch_add(1, std::memory_order_relaxed);
        });

    game::sim::SimLoop loop(world, dispatcher, std::chrono::milliseconds(10));

    std::atomic<int> executed{0};
    loop.push_command(std::make_unique<CountCommand>(executed, 1));

    // Run exactly one tick synchronously so we can assert in-same-tick
    // flushing without racing the live sim thread.
    loop.run_one_tick_for_test();

    EXPECT_EQ(executed.load(), 1);
    EXPECT_EQ(sends.load(), 1);
    // After one tick, pending buffer should be empty.
    EXPECT_EQ(dispatcher.pending_size(), 0u);
}

// ── Commands pushed after stop() are never executed ───────────────────────
TEST(SimLoop, StopDiscardsQueuedCommands) {
    game::sim::World world;
    game::session::SessionManager sessions;
    game::event::EventDispatcher dispatcher(sessions, [](auto, auto, auto){});
    game::sim::SimLoop loop(world, dispatcher, std::chrono::milliseconds(5));

    std::atomic<int> executed{0};
    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    loop.stop();

    const int before_push = executed.load();
    // After stop, commands go into the queue but are never drained.
    for (int i = 0; i < 10; ++i) {
        loop.push_command(std::make_unique<CountCommand>(executed, 1));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(executed.load(), before_push);
}

// ── tick_count() is visible to other threads between ticks ────────────────
TEST(SimLoop, TickCountIsVisibleToProducerThreads) {
    game::sim::World world;
    game::session::SessionManager sessions;
    game::event::EventDispatcher dispatcher(sessions, [](auto, auto, auto){});
    game::sim::SimLoop loop(world, dispatcher, std::chrono::milliseconds(5));

    EXPECT_EQ(loop.tick_count(), 0u);
    loop.start();
    // Wait for measurable progress.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto mid = loop.tick_count();
    EXPECT_GT(mid, 0u);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto later = loop.tick_count();
    EXPECT_GE(later, mid);
    loop.stop();
}
