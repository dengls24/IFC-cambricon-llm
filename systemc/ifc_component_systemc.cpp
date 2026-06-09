#define SC_INCLUDE_DYNAMIC_PROCESSES
#include <systemc.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#define IFC_HW_CYCLE_NO_MAIN
#include "ifc_hw_cycle_model.cpp"

namespace {

struct StageIssue {
    int command_id = -1;
    Command command;
    long long start_cycle = 0;
    long long end_cycle = 0;
    long long duration_cycles = 0;
    std::string unit_key;
    std::string module_name;
};

struct StageDone {
    int command_id = -1;
    long long observed_cycle = 0;
    std::string unit_key;
    std::string module_name;
};

struct ComponentConfig {
    double module_clock_ns = 2.5;
    int issue_width = 8;
    int queue_depth = 8;
};

struct IssueResult {
    bool ok = true;
    int issued = 0;
    bool more_ready = false;
    bool fifo_full = false;
};

std::ostream &operator<<(std::ostream &out, const StageIssue &issue) {
    out << "StageIssue{command_id=" << issue.command_id
        << ",stage=" << stage_name(stage_of(issue.command))
        << ",start=" << issue.start_cycle
        << ",end=" << issue.end_cycle
        << ",unit=" << issue.unit_key << "}";
    return out;
}

std::ostream &operator<<(std::ostream &out, const StageDone &done) {
    out << "StageDone{command_id=" << done.command_id
        << ",observed_cycle=" << done.observed_cycle
        << ",unit=" << done.unit_key << "}";
    return out;
}

long long cycle_from_time(const Timing &timing) {
    double time_ns = sc_core::sc_time_stamp().to_seconds() * 1e9;
    double cycles = timing.cycle_ns > 0.0 ? time_ns / timing.cycle_ns : time_ns;
    return static_cast<long long>(std::floor(cycles + 0.5));
}

sc_core::sc_time cycles_to_time(long long cycles, const Timing &timing) {
    return sc_core::sc_time(static_cast<double>(cycles) * timing.cycle_ns, sc_core::SC_NS);
}

long long quantize_stage_cycles(long long base_cycles, const Timing &timing, const ComponentConfig &config) {
    double base_ns = static_cast<double>(base_cycles) * timing.cycle_ns;
    double module_cycles = std::ceil(base_ns / config.module_clock_ns);
    double quantized_ns = module_cycles * config.module_clock_ns;
    return ceil_cycles(quantized_ns, timing.cycle_ns);
}

std::string unit_key_for(const Command &command) {
    std::ostringstream out;
    Stage stage = stage_of(command);
    if (stage == Stage::CaTransfer || stage == Stage::VectorTransfer || stage == Stage::DataTransfer) {
        out << "onfi_channel_" << command.channel;
    } else if (stage == Stage::ArrayRead) {
        out << "array_c" << command.channel << "_x" << command.chip << "_d" << command.die << "_p" << command.plane;
    } else {
        out << "ifc_c" << command.channel << "_x" << command.chip << "_d" << command.die;
    }
    return out.str();
}

std::string module_name_for(const Command &command) {
    std::ostringstream out;
    Stage stage = stage_of(command);
    if (stage == Stage::CaTransfer || stage == Stage::VectorTransfer || stage == Stage::DataTransfer) {
        out << "onfi_bus[ch" << command.channel << "]";
    } else if (stage == Stage::ArrayRead) {
        out << "plane_array[ch" << command.channel << ":chip" << command.chip << ":die" << command.die
            << ":plane" << command.plane << "]";
    } else {
        out << "ifc_compute[ch" << command.channel << ":chip" << command.chip << ":die" << command.die << "]";
    }
    return out.str();
}

const char *module_class_for(Stage stage) {
    if (stage == Stage::CaTransfer || stage == Stage::VectorTransfer || stage == Stage::DataTransfer) {
        return "onfi_bus";
    }
    if (stage == Stage::ArrayRead) {
        return "plane_array";
    }
    return "ifc_compute";
}

bool write_component_event(
    FILE *file,
    Stats *stats,
    long long event_cycle,
    const char *event_type,
    const char *module_name,
    int command_id,
    const Command &command,
    int active_commands) {
    Stage stage = stage_of(command);
    if (std::fprintf(
            file,
            "%d,%lld,%s,%s,%d,%s,%d,%d,%s,%s,%s,%s,%s,%d,%d,%d,%d,%lld,%lld,%lld,%d\n",
            stats->events,
            event_cycle,
            event_type,
            module_name,
            command_id,
            opcode_name(command.opcode),
            command.logical_id,
            command.slice_id,
            stage_name(stage),
            subrequest_state(stage),
            channel_state(stage),
            chip_state(stage),
            plane_state(stage),
            command.channel,
            command.chip,
            command.die,
            command.plane,
            command.stage_start_cycle,
            command.stage_end_cycle,
            command.stage_duration_cycles,
            active_commands) < 0) {
        return false;
    }
    ++stats->events;
    if (std::strcmp(event_type, "ISSUE") == 0) {
        ++stats->issue_events;
    } else {
        ++stats->complete_events;
    }
    if (active_commands > stats->max_active_commands) {
        stats->max_active_commands = active_commands;
    }
    stats->last_event_cycle = event_cycle;
    return true;
}

class IfcExecutionFabric : public sc_core::sc_module {
  public:
    sc_core::sc_fifo_in<StageIssue> issue_in;
    sc_core::sc_fifo_out<StageDone> done_out;

    SC_HAS_PROCESS(IfcExecutionFabric);

    IfcExecutionFabric(sc_core::sc_module_name name, Timing timing, ComponentConfig config)
        : sc_core::sc_module(name), timing_(timing), config_(config) {
        SC_THREAD(dispatch);
    }

    int busy_violations() const {
        return busy_violations_;
    }

    int issued() const {
        return issued_;
    }

    int completed() const {
        return completed_;
    }

    const std::map<std::string, int> &issued_by_class() const {
        return issued_by_class_;
    }

    const std::map<std::string, int> &completed_by_class() const {
        return completed_by_class_;
    }

  private:
    Timing timing_;
    ComponentConfig config_;
    std::set<std::string> busy_units_;
    std::map<std::string, int> issued_by_class_;
    std::map<std::string, int> completed_by_class_;
    int busy_violations_ = 0;
    int issued_ = 0;
    int completed_ = 0;

    void dispatch() {
        while (true) {
            StageIssue issue = issue_in.read();
            if (busy_units_.count(issue.unit_key) != 0U) {
                ++busy_violations_;
            }
            busy_units_.insert(issue.unit_key);
            ++issued_;
            ++issued_by_class_[module_class_for(stage_of(issue.command))];

            std::ostringstream name;
            name << "stage_" << issued_ << "_" << issue.unit_key;
            sc_core::sc_spawn(
                sc_core::sc_bind(&IfcExecutionFabric::run_stage, this, issue),
                name.str().c_str());
        }
    }

    void run_stage(StageIssue issue) {
        sc_core::wait(cycles_to_time(issue.duration_cycles, timing_));
        StageDone done;
        done.command_id = issue.command_id;
        done.observed_cycle = cycle_from_time(timing_);
        done.unit_key = issue.unit_key;
        done.module_name = issue.module_name;
        busy_units_.erase(issue.unit_key);
        ++completed_;
        ++completed_by_class_[module_class_for(stage_of(issue.command))];
        done_out.write(done);
    }
};

class IfcComponentController : public sc_core::sc_module {
  public:
    sc_core::sc_fifo_out<StageIssue> issue_out;
    sc_core::sc_fifo_in<StageDone> done_in;
    sc_core::sc_out<int> active_signal;
    sc_core::sc_out<int> completed_signal;
    sc_core::sc_out<int> event_signal;

    SC_HAS_PROCESS(IfcComponentController);

    IfcComponentController(
        sc_core::sc_module_name name,
        Platform platform,
        Timing timing,
        ComponentConfig config,
        const char *trace_path,
        Stats *stats)
        : sc_core::sc_module(name),
          platform_(std::move(platform)),
          timing_(timing),
          config_(config),
          trace_path_(trace_path),
          stats_(stats) {
        SC_THREAD(run);
    }

    bool ok() const {
        return ok_;
    }

    int timing_violations() const {
        return timing_violations_;
    }

  private:
    Platform platform_;
    Timing timing_;
    ComponentConfig config_;
    std::string trace_path_;
    Stats *stats_;
    ResourceState resources_;
    std::vector<Command> commands_;
    FILE *trace_file_ = nullptr;
    int active_commands_ = 0;
    bool ok_ = false;
    int timing_violations_ = 0;
    int dispatch_cycles_ = 0;
    int issue_backpressure_stalls_ = 0;
    int width_limited_dispatches_ = 0;

    void update_signals() {
        active_signal.write(active_commands_);
        completed_signal.write(stats_->completed_commands);
        event_signal.write(stats_->events);
    }

    bool command_ready_now(const Command &command, long long current_cycle) const {
        return command.status == Status::Waiting &&
               command.arrival_cycle <= current_cycle &&
               resources_free(resources_, command);
    }

    IssueResult issue_ready_commands() {
        IssueResult result;
        long long current_cycle = cycle_from_time(timing_);
        for (int i = 0; i < static_cast<int>(commands_.size()); ++i) {
            Command &command = commands_[i];
            if (command_ready_now(command, current_cycle)) {
                if (result.issued >= config_.issue_width) {
                    result.more_ready = true;
                    break;
                }
                if (issue_out.num_free() <= 0) {
                    result.more_ready = true;
                    result.fifo_full = true;
                    break;
                }
                long long base_duration_cycles = stage_duration(command, timing_);
                command.stage_duration_cycles = quantize_stage_cycles(base_duration_cycles, timing_, config_);
                command.stage_start_cycle = current_cycle;
                command.stage_end_cycle = current_cycle + command.stage_duration_cycles;
                command.status = Status::Active;
                reserve_resources(&resources_, command, i);
                ++active_commands_;
                ++result.issued;

                StageIssue issue;
                issue.command_id = i;
                issue.command = command;
                issue.start_cycle = command.stage_start_cycle;
                issue.end_cycle = command.stage_end_cycle;
                issue.duration_cycles = command.stage_duration_cycles;
                issue.unit_key = unit_key_for(command);
                issue.module_name = module_name_for(command);

                if (!write_component_event(
                        trace_file_,
                        stats_,
                        current_cycle,
                        "ISSUE",
                        issue.module_name.c_str(),
                        i,
                        command,
                        active_commands_)) {
                    result.ok = false;
                    return result;
                }
                update_signals();
                issue_out.write(issue);
            }
        }
        ++dispatch_cycles_;
        return result;
    }

    bool complete_stage(const StageDone &done) {
        if (done.command_id < 0 || done.command_id >= static_cast<int>(commands_.size())) {
            return false;
        }
        Command &command = commands_[done.command_id];
        long long current_cycle = cycle_from_time(timing_);
        if (done.observed_cycle != command.stage_end_cycle || current_cycle != command.stage_end_cycle) {
            ++timing_violations_;
        }
        if (!write_component_event(
                trace_file_,
                stats_,
                current_cycle,
                "COMPLETE",
                done.module_name.c_str(),
                done.command_id,
                command,
                active_commands_)) {
            return false;
        }
        release_resources(&resources_, command, done.command_id);
        --active_commands_;
        ++command.stage_index;
        if (command.stage_index >= stage_count(command)) {
            command.status = Status::Done;
            ++stats_->completed_commands;
        } else {
            command.status = Status::Waiting;
        }
        update_signals();
        return true;
    }

    bool drain_same_time_completions(const StageDone &first_done) {
        if (!complete_stage(first_done)) {
            return false;
        }
        sc_core::wait(sc_core::SC_ZERO_TIME);
        while (done_in.num_available() > 0) {
            StageDone done = done_in.read();
            if (!complete_stage(done)) {
                return false;
            }
        }
        return true;
    }

    bool dispatch_ready_until_blocked() {
        while (true) {
            IssueResult issue_result = issue_ready_commands();
            if (!issue_result.ok) {
                return false;
            }
            if (!issue_result.more_ready) {
                return true;
            }
            if (issue_result.fifo_full) {
                ++issue_backpressure_stalls_;
            } else {
                ++width_limited_dispatches_;
            }
            sc_core::wait(sc_core::sc_time(config_.module_clock_ns, sc_core::SC_NS));
        }
    }

    void run() {
        ok_ = run_controller();
        if (trace_file_ != nullptr) {
            std::fclose(trace_file_);
            trace_file_ = nullptr;
        }
        sc_core::sc_stop();
    }

    bool run_controller() {
        commands_ = build_commands(platform_);
        stats_->commands = static_cast<int>(commands_.size());
        trace_file_ = std::fopen(trace_path_.c_str(), "w");
        if (trace_file_ == nullptr) {
            return false;
        }
        std::fprintf(
            trace_file_,
            "event_id,event_cycle,event_type,module,command_id,opcode,logical_id,slice_id,stage,subrequest_state,"
            "channel_state,chip_state,plane_state,channel,chip,die,plane,stage_start_cycle,stage_end_cycle,"
            "duration_cycles,active_commands\n");
        update_signals();

        if (!dispatch_ready_until_blocked()) {
            return false;
        }
        while (stats_->completed_commands < stats_->commands) {
            StageDone done = done_in.read();
            if (!drain_same_time_completions(done)) {
                return false;
            }
            if (!dispatch_ready_until_blocked()) {
                return false;
            }
        }
        return active_commands_ == 0 && timing_violations_ == 0;
    }

  public:
    int dispatch_cycles() const {
        return dispatch_cycles_;
    }

    int issue_backpressure_stalls() const {
        return issue_backpressure_stalls_;
    }

    int width_limited_dispatches() const {
        return width_limited_dispatches_;
    }
};

bool write_component_stats(
    const char *path,
    const Platform &platform,
    const Timing &timing,
    const ComponentConfig &config,
    double final_systemc_time_ns,
    const Stats &stats,
    const IfcComponentController &controller,
    const IfcExecutionFabric &fabric) {
    FILE *file = std::fopen(path, "w");
    if (file == nullptr) {
        return false;
    }
    std::fprintf(file, "metric,value\n");
    std::fprintf(file, "backend,systemc_component\n");
    std::fprintf(file, "platform,%s\n", platform.name.c_str());
    std::fprintf(file, "cycle_ns,%.6f\n", timing.cycle_ns);
    std::fprintf(file, "component_module_clock_ns,%.6f\n", config.module_clock_ns);
    std::fprintf(file, "component_issue_width,%d\n", config.issue_width);
    std::fprintf(file, "component_queue_depth,%d\n", config.queue_depth);
    std::fprintf(file, "commands,%d\n", stats.commands);
    std::fprintf(file, "completed_commands,%d\n", stats.completed_commands);
    std::fprintf(file, "events,%d\n", stats.events);
    std::fprintf(file, "issue_events,%d\n", stats.issue_events);
    std::fprintf(file, "complete_events,%d\n", stats.complete_events);
    std::fprintf(file, "max_active_commands,%d\n", stats.max_active_commands);
    std::fprintf(file, "last_event_cycle,%lld\n", stats.last_event_cycle);
    std::fprintf(file, "last_event_ns,%.6f\n", final_systemc_time_ns);
    std::fprintf(file, "last_event_cycle_rounded_ns,%.6f\n", static_cast<double>(stats.last_event_cycle) * timing.cycle_ns);
    std::fprintf(file, "command_address_cycles,%lld\n", timing.command_address_cycles);
    std::fprintf(file, "read_compute_vector_cycles,%lld\n", timing.read_compute_vector_cycles);
    std::fprintf(file, "read_slice_data_cycles,%lld\n", timing.read_slice_data_cycles);
    std::fprintf(file, "array_read_cycles,%lld\n", timing.array_read_cycles);
    std::fprintf(file, "ifc_compute_cycles,%lld\n", timing.ifc_compute_cycles);
    std::fprintf(file, "channels,%d\n", platform.channels);
    std::fprintf(file, "chips_per_channel,%d\n", platform.chips_per_channel);
    std::fprintf(file, "dies_per_chip,%d\n", platform.dies_per_chip);
    std::fprintf(file, "planes_per_die,%d\n", platform.planes_per_die);
    std::fprintf(file, "fabric_issued,%d\n", fabric.issued());
    std::fprintf(file, "fabric_completed,%d\n", fabric.completed());
    std::fprintf(file, "fabric_busy_violations,%d\n", fabric.busy_violations());
    std::fprintf(file, "controller_timing_violations,%d\n", controller.timing_violations());
    std::fprintf(file, "dispatch_cycles,%d\n", controller.dispatch_cycles());
    std::fprintf(file, "issue_backpressure_stalls,%d\n", controller.issue_backpressure_stalls());
    std::fprintf(file, "width_limited_dispatches,%d\n", controller.width_limited_dispatches());
    return std::fclose(file) == 0;
}

double read_metric_double(const char *path, const char *metric) {
    FILE *file = std::fopen(path, "r");
    if (file == nullptr) {
        return -1.0;
    }
    char line[512];
    double value = -1.0;
    while (std::fgets(line, sizeof(line), file) != nullptr) {
        char key[128];
        double parsed_value = -1.0;
        if (std::sscanf(line, "%127[^,],%lf", key, &parsed_value) == 2 &&
            std::strcmp(key, metric) == 0) {
            value = parsed_value;
            break;
        }
    }
    std::fclose(file);
    return value;
}

bool write_component_compare(const char *path, const char *c_stats_path, const Timing &timing, const Stats &stats, double final_systemc_time_ns) {
    long long c_events = read_metric_ll(c_stats_path, "events");
    long long c_completed = read_metric_ll(c_stats_path, "completed_commands");
    long long c_last = read_metric_ll(c_stats_path, "last_event_cycle");
    double c_last_ns = read_metric_double(c_stats_path, "last_event_ns");
    long long last_delta = c_last >= 0 ? stats.last_event_cycle - c_last : -1;
    long long last_tolerance = c_last >= 0 ? static_cast<long long>(std::ceil(static_cast<double>(c_last) * 0.01)) : 0;
    if (last_tolerance < 100) {
        last_tolerance = 100;
    }
    double final_delta_ns = c_last_ns >= 0.0 ? final_systemc_time_ns - c_last_ns : -1.0;
    double final_tolerance_ns = static_cast<double>(last_tolerance) * timing.cycle_ns;

    FILE *file = std::fopen(path, "w");
    if (file == nullptr) {
        return false;
    }
    std::fprintf(file, "metric,c_backend,systemc_component,delta,status\n");
    std::fprintf(
        file,
        "events,%lld,%d,%lld,%s\n",
        c_events,
        stats.events,
        c_events >= 0 ? static_cast<long long>(stats.events) - c_events : -1,
        c_events == stats.events ? "PASS" : "FAIL");
    std::fprintf(
        file,
        "completed_commands,%lld,%d,%lld,%s\n",
        c_completed,
        stats.completed_commands,
        c_completed >= 0 ? static_cast<long long>(stats.completed_commands) - c_completed : -1,
        c_completed == stats.completed_commands ? "PASS" : "FAIL");
    std::fprintf(
        file,
        "last_event_cycle,%lld,%lld,%lld,%s\n",
        c_last,
        stats.last_event_cycle,
        last_delta,
        (c_last >= 0 && std::llabs(last_delta) <= last_tolerance) ? "PASS" : "FAIL");
    std::fprintf(
        file,
        "final_time_ns,%.6f,%.6f,%.6f,%s\n",
        c_last_ns,
        final_systemc_time_ns,
        final_delta_ns,
        (c_last_ns >= 0.0 && std::fabs(final_delta_ns) <= final_tolerance_ns) ? "PASS" : "FAIL");
    return std::fclose(file) == 0;
}

bool write_component_module_stats(const char *path, const IfcExecutionFabric &fabric) {
    FILE *file = std::fopen(path, "w");
    if (file == nullptr) {
        return false;
    }
    std::fprintf(file, "module_class,issued,completed\n");
    const char *classes[] = {"onfi_bus", "plane_array", "ifc_compute"};
    for (const char *module_class : classes) {
        int issued = 0;
        int completed = 0;
        std::map<std::string, int>::const_iterator issue_it = fabric.issued_by_class().find(module_class);
        std::map<std::string, int>::const_iterator done_it = fabric.completed_by_class().find(module_class);
        if (issue_it != fabric.issued_by_class().end()) {
            issued = issue_it->second;
        }
        if (done_it != fabric.completed_by_class().end()) {
            completed = done_it->second;
        }
        std::fprintf(file, "%s,%d,%d\n", module_class, issued, completed);
    }
    return std::fclose(file) == 0;
}

std::string displayed_vcd_path(const char *vcd_path) {
    std::string path = vcd_path;
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".vcd") {
        return path;
    }
    return path + ".vcd";
}

}  // namespace

int sc_main(int argc, char **argv) {
    const char *platform_csv = "configs/default_platforms.csv";
    const char *trace_path = "results/systemc_component_trace.csv";
    const char *stats_path = "results/systemc_component_stats.csv";
    const char *compare_path = "results/systemc_component_compare.csv";
    const char *module_stats_path = "results/systemc_component_modules.csv";
    const char *vcd_path = "results/systemc_component.vcd";
    const char *c_stats_path = "results/ssdsim_ifc_event_stats.csv";
    ComponentConfig component_config;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--platforms-csv") == 0 && i + 1 < argc) {
            platform_csv = argv[++i];
        } else if (std::strcmp(argv[i], "--trace") == 0 && i + 1 < argc) {
            trace_path = argv[++i];
        } else if (std::strcmp(argv[i], "--stats") == 0 && i + 1 < argc) {
            stats_path = argv[++i];
        } else if (std::strcmp(argv[i], "--compare") == 0 && i + 1 < argc) {
            compare_path = argv[++i];
        } else if (std::strcmp(argv[i], "--module-stats") == 0 && i + 1 < argc) {
            module_stats_path = argv[++i];
        } else if (std::strcmp(argv[i], "--vcd") == 0 && i + 1 < argc) {
            vcd_path = argv[++i];
        } else if (std::strcmp(argv[i], "--no-vcd") == 0) {
            vcd_path = nullptr;
        } else if (std::strcmp(argv[i], "--c-stats") == 0 && i + 1 < argc) {
            c_stats_path = argv[++i];
        } else if (std::strcmp(argv[i], "--module-clock-ns") == 0 && i + 1 < argc) {
            component_config.module_clock_ns = std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--issue-width") == 0 && i + 1 < argc) {
            component_config.issue_width = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--queue-depth") == 0 && i + 1 < argc) {
            component_config.queue_depth = std::atoi(argv[++i]);
        } else {
            std::fprintf(
                stderr,
                "usage: %s [--platforms-csv path] [--trace path] [--stats path] [--compare path] [--module-stats path] [--vcd path|--no-vcd] [--c-stats path] [--module-clock-ns ns] [--issue-width n] [--queue-depth n]\n",
                argv[0]);
            return 1;
        }
    }
    if (component_config.module_clock_ns <= 0.0 || component_config.issue_width <= 0 || component_config.queue_depth <= 0) {
        std::fprintf(stderr, "error: invalid SystemC component model parameters\n");
        return 1;
    }

    Platform platform;
    if (!parse_platform_csv(platform_csv, &platform)) {
        std::fprintf(stderr, "error: failed to load platform CSV %s\n", platform_csv);
        return 1;
    }
    if (!platform_supported(platform)) {
        std::fprintf(stderr, "error: first platform dimensions exceed SystemC component model limits\n");
        return 1;
    }

    Timing timing = derive_timing(platform);
    Stats stats;
    sc_core::sc_fifo<StageIssue> issue_fifo("issue_fifo", component_config.queue_depth);
    sc_core::sc_fifo<StageDone> done_fifo("done_fifo", 4096);
    sc_core::sc_signal<int> active_signal("active_commands");
    sc_core::sc_signal<int> completed_signal("completed_commands");
    sc_core::sc_signal<int> event_signal("events");

    IfcComponentController controller("ifc_component_controller", platform, timing, component_config, trace_path, &stats);
    IfcExecutionFabric fabric("ifc_execution_fabric", timing, component_config);
    controller.issue_out(issue_fifo);
    controller.done_in(done_fifo);
    controller.active_signal(active_signal);
    controller.completed_signal(completed_signal);
    controller.event_signal(event_signal);
    fabric.issue_in(issue_fifo);
    fabric.done_out(done_fifo);

    sc_core::sc_trace_file *vcd = nullptr;
    if (vcd_path != nullptr) {
        vcd = sc_core::sc_create_vcd_trace_file(vcd_path);
        if (vcd != nullptr) {
            sc_core::sc_trace(vcd, active_signal, "active_commands");
            sc_core::sc_trace(vcd, completed_signal, "completed_commands");
            sc_core::sc_trace(vcd, event_signal, "events");
        }
    }

    sc_core::sc_start();
    double final_systemc_time_ns = sc_core::sc_time_stamp().to_seconds() * 1e9;

    if (vcd != nullptr) {
        sc_core::sc_close_vcd_trace_file(vcd);
    }

    if (!controller.ok()) {
        std::fprintf(stderr, "error: SystemC component controller failed\n");
        return 1;
    }
    if (fabric.busy_violations() != 0 || fabric.issued() != fabric.completed()) {
        std::fprintf(stderr, "error: SystemC execution fabric resource validation failed\n");
        return 1;
    }
    if (!write_component_stats(stats_path, platform, timing, component_config, final_systemc_time_ns, stats, controller, fabric)) {
        std::fprintf(stderr, "error: failed to write stats %s\n", stats_path);
        return 1;
    }
    if (!write_component_module_stats(module_stats_path, fabric)) {
        std::fprintf(stderr, "error: failed to write module stats %s\n", module_stats_path);
        return 1;
    }
    if (!write_component_compare(compare_path, c_stats_path, timing, stats, final_systemc_time_ns)) {
        std::fprintf(stderr, "error: failed to write compare %s\n", compare_path);
        return 1;
    }

    std::printf("passed: SystemC component model\n");
    std::printf("trace_csv: %s\n", trace_path);
    std::printf("stats_csv: %s\n", stats_path);
    std::printf("compare_csv: %s\n", compare_path);
    std::printf("module_stats_csv: %s\n", module_stats_path);
    if (vcd_path != nullptr) {
        std::printf("vcd: %s\n", displayed_vcd_path(vcd_path).c_str());
    }
    std::printf("events: %d\n", stats.events);
    std::printf("last_event_cycle: %lld\n", stats.last_event_cycle);
    std::printf("systemc_time_ns: %.6f\n", final_systemc_time_ns);
    return 0;
}
