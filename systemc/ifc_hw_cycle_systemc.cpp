#include <systemc.h>

#include <memory>
#include <utility>

#define IFC_HW_CYCLE_NO_MAIN
#include "ifc_hw_cycle_model.cpp"

namespace {

class IfcSystemCController : public sc_core::sc_module {
  public:
    SC_HAS_PROCESS(IfcSystemCController);

    IfcSystemCController(
        sc_core::sc_module_name name,
        Platform platform,
        Timing timing,
        const char *trace_path,
        Stats *stats)
        : sc_core::sc_module(name),
          platform_(std::move(platform)),
          timing_(timing),
          trace_path_(trace_path),
          stats_(stats) {
        SC_THREAD(run);
    }

    bool ok() const {
        return ok_;
    }

  private:
    Platform platform_;
    Timing timing_;
    std::string trace_path_;
    Stats *stats_;
    bool ok_ = false;

    void run() {
        ok_ = run_event_loop();
        sc_core::sc_stop();
    }

    bool advance_to_cycle(long long *current_cycle, long long next_cycle) {
        if (next_cycle < *current_cycle) {
            return false;
        }
        long long delta_cycles = next_cycle - *current_cycle;
        if (delta_cycles > 0) {
            sc_core::sc_time delta_time(
                static_cast<double>(delta_cycles) * timing_.cycle_ns,
                sc_core::SC_NS);
            sc_core::wait(delta_time);
        }
        *current_cycle = next_cycle;
        return true;
    }

    bool run_event_loop() {
        std::vector<Command> commands = build_commands(platform_);
        std::unique_ptr<ResourceState> resources(new ResourceState());
        FILE *file = std::fopen(trace_path_.c_str(), "w");
        if (file == nullptr) {
            return false;
        }
        std::fprintf(
            file,
            "event_id,event_cycle,event_type,command_id,opcode,logical_id,slice_id,stage,subrequest_state,"
            "channel_state,chip_state,plane_state,channel,chip,die,plane,stage_start_cycle,stage_end_cycle,"
            "duration_cycles,active_commands\n");

        stats_->commands = static_cast<int>(commands.size());
        int active_commands = 0;
        long long current_cycle = 0;
        long long guard = 0;
        while (stats_->completed_commands < stats_->commands && guard < 10000000LL) {
            int completed_this_cycle = 0;
            int issued_this_cycle = 0;
            long long next_cycle = kInfCycle;

            for (int i = 0; i < static_cast<int>(commands.size()); ++i) {
                Command &command = commands[i];
                if (command.status == Status::Active && command.stage_end_cycle == current_cycle) {
                    if (!write_event(file, stats_, current_cycle, "COMPLETE", i, command, active_commands)) {
                        std::fclose(file);
                        return false;
                    }
                    release_resources(resources.get(), command, i);
                    --active_commands;
                    ++completed_this_cycle;
                    ++command.stage_index;
                    if (command.stage_index >= stage_count(command)) {
                        command.status = Status::Done;
                        ++stats_->completed_commands;
                    } else {
                        command.status = Status::Waiting;
                    }
                }
            }

            for (int i = 0; i < static_cast<int>(commands.size()); ++i) {
                Command &command = commands[i];
                if (command.status == Status::Waiting &&
                    command.arrival_cycle <= current_cycle &&
                    resources_free(*resources, command)) {
                    command.stage_duration_cycles = stage_duration(command, timing_);
                    command.stage_start_cycle = current_cycle;
                    command.stage_end_cycle = current_cycle + command.stage_duration_cycles;
                    command.status = Status::Active;
                    reserve_resources(resources.get(), command, i);
                    ++active_commands;
                    ++issued_this_cycle;
                    if (!write_event(file, stats_, current_cycle, "ISSUE", i, command, active_commands)) {
                        std::fclose(file);
                        return false;
                    }
                }
            }

            if (stats_->completed_commands >= stats_->commands) {
                break;
            }
            for (const Command &command : commands) {
                if (command.status == Status::Active && command.stage_end_cycle < next_cycle) {
                    next_cycle = command.stage_end_cycle;
                } else if (command.status == Status::Waiting &&
                           command.arrival_cycle > current_cycle &&
                           command.arrival_cycle < next_cycle) {
                    next_cycle = command.arrival_cycle;
                }
            }
            if (next_cycle == kInfCycle) {
                std::fclose(file);
                return false;
            }
            if (next_cycle == current_cycle && completed_this_cycle == 0 && issued_this_cycle == 0) {
                std::fclose(file);
                return false;
            }
            if (!advance_to_cycle(&current_cycle, next_cycle)) {
                std::fclose(file);
                return false;
            }
            ++guard;
        }
        if (std::fclose(file) != 0) {
            return false;
        }
        return stats_->completed_commands == stats_->commands;
    }
};

}  // namespace

int sc_main(int argc, char **argv) {
    const char *platform_csv = "configs/default_platforms.csv";
    const char *trace_path = "results/systemc_cycle_trace.csv";
    const char *stats_path = "results/systemc_cycle_stats.csv";
    const char *compare_path = "results/systemc_cycle_compare.csv";
    const char *c_stats_path = "results/ssdsim_ifc_event_stats.csv";

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--platforms-csv") == 0 && i + 1 < argc) {
            platform_csv = argv[++i];
        } else if (std::strcmp(argv[i], "--trace") == 0 && i + 1 < argc) {
            trace_path = argv[++i];
        } else if (std::strcmp(argv[i], "--stats") == 0 && i + 1 < argc) {
            stats_path = argv[++i];
        } else if (std::strcmp(argv[i], "--compare") == 0 && i + 1 < argc) {
            compare_path = argv[++i];
        } else if (std::strcmp(argv[i], "--c-stats") == 0 && i + 1 < argc) {
            c_stats_path = argv[++i];
        } else {
            std::fprintf(
                stderr,
                "usage: %s [--platforms-csv path] [--trace path] [--stats path] [--compare path] [--c-stats path]\n",
                argv[0]);
            return 1;
        }
    }

    Platform platform;
    if (!parse_platform_csv(platform_csv, &platform)) {
        std::fprintf(stderr, "error: failed to load platform CSV %s\n", platform_csv);
        return 1;
    }
    if (!platform_supported(platform)) {
        std::fprintf(stderr, "error: first platform dimensions exceed SystemC cycle model limits\n");
        return 1;
    }

    Timing timing = derive_timing(platform);
    Stats stats;
    IfcSystemCController controller("ifc_systemc_controller", platform, timing, trace_path, &stats);
    sc_core::sc_start();

    if (!controller.ok()) {
        std::fprintf(stderr, "error: SystemC cycle event loop failed\n");
        return 1;
    }
    if (!write_stats_named(stats_path, "systemc_hw_cycle", platform, timing, stats)) {
        std::fprintf(stderr, "error: failed to write stats %s\n", stats_path);
        return 1;
    }
    if (!write_compare_named(compare_path, "systemc_cycle", c_stats_path, stats)) {
        std::fprintf(stderr, "error: failed to write compare %s\n", compare_path);
        return 1;
    }

    std::printf("passed: SystemC hardware cycle model\n");
    std::printf("trace_csv: %s\n", trace_path);
    std::printf("stats_csv: %s\n", stats_path);
    std::printf("compare_csv: %s\n", compare_path);
    std::printf("events: %d\n", stats.events);
    std::printf("last_event_cycle: %lld\n", stats.last_event_cycle);
    std::printf("systemc_time_ns: %.6f\n", sc_core::sc_time_stamp().to_seconds() * 1e9);
    return 0;
}
