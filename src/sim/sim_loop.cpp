#include "sim/sim_loop.h"

#include "event/dispatcher.h"
#include "log/logger.h"

namespace game::sim {

SimLoop::SimLoop(World& world, game::event::EventDispatcher& events,
                 std::chrono::milliseconds tick_period)
    : world_(world), events_(events), tick_period_(tick_period) {}

SimLoop::~SimLoop() {
    stop();
}

void SimLoop::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;  // already running
    }
    thread_ = std::thread([this] { run(); });
    LOG_INF("sim: loop started (period={}ms)", tick_period_.count());
}

void SimLoop::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;  // already stopped
    }
    if (thread_.joinable()) thread_.join();

    // Drain and discard anything producers pushed after the running flag
    // flipped. Their reply paths have no receiver anyway.
    std::unique_ptr<Command> cmd;
    size_t dropped = 0;
    while (queue_.try_dequeue(cmd)) ++dropped;
    if (dropped > 0) LOG_INF("sim: dropped {} queued command(s) at shutdown", dropped);
    LOG_INF("sim: loop stopped");
}

void SimLoop::push_command(std::unique_ptr<Command> cmd) {
    queue_.enqueue(std::move(cmd));
}

void SimLoop::process_one_tick() {
    // 1. Drain queue.
    std::unique_ptr<Command> cmd;
    CommandContext ctx{world_, events_, world_.tick};
    while (queue_.try_dequeue(cmd)) {
        try {
            cmd->execute(ctx);
        } catch (const std::exception& e) {
            LOG_ERR("sim: command '{}' threw: {}", cmd->name(), e.what());
        }
    }

    // 2. Advance world.
    world_.advance();

    // 3. Expose the new tick count before flushing, so any event consumer
    //    that re-reads tick_count() during fanout sees the current value.
    tick_count_.store(world_.tick, std::memory_order_relaxed);

    // 4. Flush events.
    events_.flush();
}

void SimLoop::run_one_tick_for_test() {
    process_one_tick();
}

void SimLoop::run() {
    using clock = std::chrono::steady_clock;
    while (running_.load(std::memory_order_relaxed)) {
        const auto tick_start = clock::now();

        process_one_tick();

        const auto elapsed = clock::now() - tick_start;
        if (elapsed > tick_period_) {
            // No catch-up: drop any leftover budget and start the next tick
            // immediately. Logged once per overrun so we notice the pattern
            // without drowning the log on a stall burst.
            LOG_WRN("sim: tick overrun ({}ms > {}ms budget)",
                    std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(),
                    tick_period_.count());
            continue;
        }
        std::this_thread::sleep_for(tick_period_ - elapsed);
    }
}

} // namespace game::sim
