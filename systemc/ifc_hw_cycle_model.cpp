#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int kReadSlicesPerRequest = 4;
constexpr int kSampleTiles = 16;
constexpr int kMaxChannels = 64;
constexpr int kMaxChipsPerChannel = 128;
constexpr int kMaxDiesPerChip = 8;
constexpr int kMaxPlanesPerDie = 8;
constexpr long long kInfCycle = 0x3fffffffffffffffLL;

struct Platform {
    std::string name = "cam_llm_s";
    int channels = 8;
    int chips_per_channel = 2;
    int dies_per_chip = 2;
    int planes_per_die = 2;
    int compute_cores_per_die = 1;
    int page_bytes = 16384;
    double array_read_us = 30.0;
    double onfi_rate_MTps = 1000.0;
    int onfi_bus_width_bits = 8;
    double ifc_frequency_hz = 1000000000.0;
    double ifc_ops_per_core_cycle = 64.0;
};

struct Timing {
    double cycle_ns = 1.0;
    long long command_address_cycles = 7;
    long long read_compute_vector_cycles = 256;
    long long read_slice_data_cycles = 4096;
    long long array_read_cycles = 30000;
    long long ifc_compute_cycles = 1024;
};

enum class Opcode {
    ReadCompute,
    ReadSlice
};

enum class Stage {
    CaTransfer,
    VectorTransfer,
    ArrayRead,
    IfcCompute,
    DataTransfer,
    Done
};

enum class Status {
    Waiting,
    Active,
    Done
};

struct Command {
    Opcode opcode = Opcode::ReadCompute;
    int logical_id = 0;
    int slice_id = -1;
    int channel = 0;
    int chip = 0;
    int die = 0;
    int plane = 0;
    int stage_index = 0;
    Status status = Status::Waiting;
    long long arrival_cycle = 0;
    long long stage_start_cycle = -1;
    long long stage_end_cycle = -1;
    long long stage_duration_cycles = 0;
};

struct ResourceState {
    int channel_active[kMaxChannels];
    int chip_active[kMaxChannels][kMaxChipsPerChannel];
    int plane_active[kMaxChannels][kMaxChipsPerChannel][kMaxDiesPerChip][kMaxPlanesPerDie];
    int compute_active[kMaxChannels][kMaxChipsPerChannel][kMaxDiesPerChip];

    ResourceState() {
        for (int channel = 0; channel < kMaxChannels; ++channel) {
            channel_active[channel] = -1;
            for (int chip = 0; chip < kMaxChipsPerChannel; ++chip) {
                chip_active[channel][chip] = -1;
                for (int die = 0; die < kMaxDiesPerChip; ++die) {
                    compute_active[channel][chip][die] = -1;
                    for (int plane = 0; plane < kMaxPlanesPerDie; ++plane) {
                        plane_active[channel][chip][die][plane] = -1;
                    }
                }
            }
        }
    }
};

struct Stats {
    int commands = 0;
    int completed_commands = 0;
    int events = 0;
    int issue_events = 0;
    int complete_events = 0;
    int max_active_commands = 0;
    long long last_event_cycle = 0;
};

long long ceil_cycles(double duration_ns, double cycle_ns) {
    long long cycles = static_cast<long long>(duration_ns / cycle_ns);
    if (static_cast<double>(cycles) * cycle_ns < duration_ns) {
        ++cycles;
    }
    return cycles > 0 ? cycles : 1;
}

bool parse_platform_csv(const char *path, Platform *platform) {
    FILE *file = std::fopen(path, "r");
    if (file == nullptr) {
        return false;
    }
    char line[4096];
    if (std::fgets(line, sizeof(line), file) == nullptr ||
        std::fgets(line, sizeof(line), file) == nullptr) {
        std::fclose(file);
        return false;
    }
    std::fclose(file);

    char *fields[32];
    int count = 0;
    char *token = std::strtok(line, ",\n\r");
    while (token != nullptr && count < 32) {
        fields[count++] = token;
        token = std::strtok(nullptr, ",\n\r");
    }
    if (count < 19) {
        return false;
    }
    platform->name = fields[0];
    platform->channels = std::atoi(fields[2]);
    platform->chips_per_channel = std::atoi(fields[3]);
    platform->dies_per_chip = std::atoi(fields[4]);
    platform->planes_per_die = std::atoi(fields[5]);
    platform->compute_cores_per_die = std::atoi(fields[6]);
    platform->page_bytes = std::atoi(fields[7]);
    platform->array_read_us = std::atof(fields[8]);
    platform->onfi_rate_MTps = std::atof(fields[10]);
    platform->onfi_bus_width_bits = std::atoi(fields[11]);
    platform->ifc_frequency_hz = std::atof(fields[12]) * 1e6;
    platform->ifc_ops_per_core_cycle = std::atof(fields[13]);
    return true;
}

bool platform_supported(const Platform &platform) {
    return platform.channels > 0 && platform.channels <= kMaxChannels &&
           platform.chips_per_channel > 0 && platform.chips_per_channel <= kMaxChipsPerChannel &&
           platform.dies_per_chip > 0 && platform.dies_per_chip <= kMaxDiesPerChip &&
           platform.planes_per_die > 0 && platform.planes_per_die <= kMaxPlanesPerDie;
}

Timing derive_timing(const Platform &platform) {
    Timing timing;
    const int cores_per_channel =
        platform.chips_per_channel * platform.dies_per_chip * platform.compute_cores_per_die;
    const double tile_height = std::sqrt(static_cast<double>(cores_per_channel) * platform.page_bytes);
    const double tile_width = static_cast<double>(platform.channels) * tile_height;
    const double channel_bw =
        platform.onfi_rate_MTps * 1e6 * static_cast<double>(platform.onfi_bus_width_bits) / 8.0;
    const double cycle_ns = platform.ifc_frequency_hz > 0.0 ? 1e9 / platform.ifc_frequency_hz : 1.0;
    const double onfi_cycle_ns = platform.onfi_rate_MTps > 0.0 ? 1000.0 / platform.onfi_rate_MTps : cycle_ns;
    const double compute_ops =
        tile_height * (tile_width / static_cast<double>(platform.channels));
    const double compute_ops_per_s =
        static_cast<double>(platform.compute_cores_per_die) *
        platform.ifc_frequency_hz *
        platform.ifc_ops_per_core_cycle;

    timing.cycle_ns = cycle_ns;
    timing.command_address_cycles = ceil_cycles(7.0 * onfi_cycle_ns, cycle_ns);
    timing.read_compute_vector_cycles =
        ceil_cycles(tile_width / static_cast<double>(platform.channels) / channel_bw * 1e9, cycle_ns);
    timing.read_slice_data_cycles =
        ceil_cycles((static_cast<double>(platform.page_bytes) / kReadSlicesPerRequest) / channel_bw * 1e9, cycle_ns);
    timing.array_read_cycles = ceil_cycles(platform.array_read_us * 1000.0, cycle_ns);
    timing.ifc_compute_cycles =
        compute_ops_per_s > 0.0 ? ceil_cycles(compute_ops / compute_ops_per_s * 1e9, cycle_ns) : 1;
    return timing;
}

const char *opcode_name(Opcode opcode) {
    return opcode == Opcode::ReadCompute ? "READ_COMPUTE" : "READ_SLICE";
}

Stage stage_of(const Command &command) {
    if (command.opcode == Opcode::ReadCompute) {
        switch (command.stage_index) {
        case 0:
            return Stage::CaTransfer;
        case 1:
            return Stage::VectorTransfer;
        case 2:
            return Stage::ArrayRead;
        case 3:
            return Stage::IfcCompute;
        default:
            return Stage::Done;
        }
    }
    switch (command.stage_index) {
    case 0:
        return Stage::CaTransfer;
    case 1:
        return Stage::DataTransfer;
    default:
        return Stage::Done;
    }
}

const char *stage_name(Stage stage) {
    switch (stage) {
    case Stage::CaTransfer:
        return "SSDSIM_CA_TRANSFER";
    case Stage::VectorTransfer:
        return "IFC_VECTOR_TRANSFER";
    case Stage::ArrayRead:
        return "SSDSIM_ARRAY_READ";
    case Stage::IfcCompute:
        return "IFC_COMPUTE";
    case Stage::DataTransfer:
        return "SSDSIM_DATA_TRANSFER";
    default:
        return "DONE";
    }
}

const char *subrequest_state(Stage stage) {
    switch (stage) {
    case Stage::CaTransfer:
        return "SR_R_C_A_TRANSFER";
    case Stage::VectorTransfer:
        return "SR_IFC_VECTOR_TRANSFER";
    case Stage::ArrayRead:
        return "SR_R_READ";
    case Stage::IfcCompute:
        return "SR_IFC_COMPUTE";
    case Stage::DataTransfer:
        return "SR_R_DATA_TRANSFER";
    default:
        return "SR_COMPLETE";
    }
}

const char *channel_state(Stage stage) {
    switch (stage) {
    case Stage::CaTransfer:
        return "CHANNEL_C_A_TRANSFER";
    case Stage::VectorTransfer:
    case Stage::DataTransfer:
        return "CHANNEL_DATA_TRANSFER";
    default:
        return "CHANNEL_IDLE";
    }
}

const char *chip_state(Stage stage) {
    switch (stage) {
    case Stage::CaTransfer:
        return "CHIP_C_A_TRANSFER";
    case Stage::VectorTransfer:
    case Stage::DataTransfer:
        return "CHIP_DATA_TRANSFER";
    case Stage::ArrayRead:
        return "CHIP_READ_BUSY";
    case Stage::IfcCompute:
        return "CHIP_IFC_COMPUTE";
    default:
        return "CHIP_IDLE";
    }
}

const char *plane_state(Stage stage) {
    switch (stage) {
    case Stage::CaTransfer:
        return "PLANE_CMD_LATCH";
    case Stage::VectorTransfer:
        return "PLANE_INPUT_LATCH";
    case Stage::ArrayRead:
        return "PLANE_ARRAY_BUSY";
    case Stage::IfcCompute:
        return "PLANE_IFC_COMPUTE";
    case Stage::DataTransfer:
        return "PLANE_DATA_REGISTER";
    default:
        return "PLANE_IDLE";
    }
}

int stage_count(const Command &command) {
    return command.opcode == Opcode::ReadCompute ? 4 : 2;
}

long long stage_duration(const Command &command, const Timing &timing) {
    switch (stage_of(command)) {
    case Stage::CaTransfer:
        return timing.command_address_cycles;
    case Stage::VectorTransfer:
        return timing.read_compute_vector_cycles;
    case Stage::ArrayRead:
        return timing.array_read_cycles;
    case Stage::IfcCompute:
        return timing.ifc_compute_cycles;
    case Stage::DataTransfer:
        return timing.read_slice_data_cycles;
    default:
        return 1;
    }
}

std::vector<Command> build_commands(const Platform &platform) {
    std::vector<Command> commands;
    commands.reserve(512);
    for (int tile_id = 0; tile_id < kSampleTiles; ++tile_id) {
        int chip = tile_id % platform.chips_per_channel;
        int die = (tile_id / platform.chips_per_channel) % platform.dies_per_chip;
        int plane = (tile_id / (platform.chips_per_channel * platform.dies_per_chip)) % platform.planes_per_die;
        for (int channel = 0; channel < platform.channels; ++channel) {
            Command command;
            command.opcode = Opcode::ReadCompute;
            command.logical_id = tile_id;
            command.channel = channel;
            command.chip = chip;
            command.die = die;
            command.plane = plane;
            commands.push_back(command);
        }
        if ((tile_id + 1) % kReadSlicesPerRequest == 0) {
            int read_plane = (plane + 1) % platform.planes_per_die;
            for (int slice_id = 0; slice_id < kReadSlicesPerRequest; ++slice_id) {
                for (int channel = 0; channel < platform.channels; ++channel) {
                    Command command;
                    command.opcode = Opcode::ReadSlice;
                    command.logical_id = tile_id / kReadSlicesPerRequest;
                    command.slice_id = slice_id;
                    command.channel = channel;
                    command.chip = chip;
                    command.die = die;
                    command.plane = read_plane;
                    commands.push_back(command);
                }
            }
        }
    }
    return commands;
}

bool resources_free(const ResourceState &resources, const Command &command) {
    Stage stage = stage_of(command);
    if (stage == Stage::CaTransfer || stage == Stage::VectorTransfer || stage == Stage::DataTransfer) {
        return resources.channel_active[command.channel] < 0 &&
               resources.chip_active[command.channel][command.chip] < 0;
    }
    if (stage == Stage::ArrayRead) {
        return resources.chip_active[command.channel][command.chip] < 0 &&
               resources.plane_active[command.channel][command.chip][command.die][command.plane] < 0;
    }
    return resources.chip_active[command.channel][command.chip] < 0 &&
           resources.plane_active[command.channel][command.chip][command.die][command.plane] < 0 &&
           resources.compute_active[command.channel][command.chip][command.die] < 0;
}

void reserve_resources(ResourceState *resources, const Command &command, int command_id) {
    Stage stage = stage_of(command);
    if (stage == Stage::CaTransfer || stage == Stage::VectorTransfer || stage == Stage::DataTransfer) {
        resources->channel_active[command.channel] = command_id;
        resources->chip_active[command.channel][command.chip] = command_id;
    } else if (stage == Stage::ArrayRead) {
        resources->chip_active[command.channel][command.chip] = command_id;
        resources->plane_active[command.channel][command.chip][command.die][command.plane] = command_id;
    } else {
        resources->chip_active[command.channel][command.chip] = command_id;
        resources->plane_active[command.channel][command.chip][command.die][command.plane] = command_id;
        resources->compute_active[command.channel][command.chip][command.die] = command_id;
    }
}

void release_resources(ResourceState *resources, const Command &command, int command_id) {
    if (resources->channel_active[command.channel] == command_id) {
        resources->channel_active[command.channel] = -1;
    }
    if (resources->chip_active[command.channel][command.chip] == command_id) {
        resources->chip_active[command.channel][command.chip] = -1;
    }
    if (resources->plane_active[command.channel][command.chip][command.die][command.plane] == command_id) {
        resources->plane_active[command.channel][command.chip][command.die][command.plane] = -1;
    }
    if (resources->compute_active[command.channel][command.chip][command.die] == command_id) {
        resources->compute_active[command.channel][command.chip][command.die] = -1;
    }
}

[[maybe_unused]] bool write_event(
    FILE *file,
    Stats *stats,
    long long event_cycle,
    const char *event_type,
    int command_id,
    const Command &command,
    int active_commands) {
    Stage stage = stage_of(command);
    if (std::fprintf(
            file,
            "%d,%lld,%s,%d,%s,%d,%d,%s,%s,%s,%s,%s,%d,%d,%d,%d,%lld,%lld,%lld,%d\n",
            stats->events,
            event_cycle,
            event_type,
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

#ifndef IFC_HW_CYCLE_NO_MAIN
bool run_event_loop(
    const Platform &platform,
    const Timing &timing,
    const char *trace_path,
    Stats *stats) {
    std::vector<Command> commands = build_commands(platform);
    ResourceState resources;
    FILE *file = std::fopen(trace_path, "w");
    if (file == nullptr) {
        return false;
    }
    std::fprintf(
        file,
        "event_id,event_cycle,event_type,command_id,opcode,logical_id,slice_id,stage,subrequest_state,"
        "channel_state,chip_state,plane_state,channel,chip,die,plane,stage_start_cycle,stage_end_cycle,"
        "duration_cycles,active_commands\n");

    stats->commands = static_cast<int>(commands.size());
    int active_commands = 0;
    long long current_cycle = 0;
    long long guard = 0;
    while (stats->completed_commands < stats->commands && guard < 10000000LL) {
        int completed_this_cycle = 0;
        int issued_this_cycle = 0;
        long long next_cycle = kInfCycle;

        for (int i = 0; i < static_cast<int>(commands.size()); ++i) {
            Command &command = commands[i];
            if (command.status == Status::Active && command.stage_end_cycle == current_cycle) {
                if (!write_event(file, stats, current_cycle, "COMPLETE", i, command, active_commands)) {
                    std::fclose(file);
                    return false;
                }
                release_resources(&resources, command, i);
                --active_commands;
                ++completed_this_cycle;
                ++command.stage_index;
                if (command.stage_index >= stage_count(command)) {
                    command.status = Status::Done;
                    ++stats->completed_commands;
                } else {
                    command.status = Status::Waiting;
                }
            }
        }

        for (int i = 0; i < static_cast<int>(commands.size()); ++i) {
            Command &command = commands[i];
            if (command.status == Status::Waiting &&
                command.arrival_cycle <= current_cycle &&
                resources_free(resources, command)) {
                command.stage_duration_cycles = stage_duration(command, timing);
                command.stage_start_cycle = current_cycle;
                command.stage_end_cycle = current_cycle + command.stage_duration_cycles;
                command.status = Status::Active;
                reserve_resources(&resources, command, i);
                ++active_commands;
                ++issued_this_cycle;
                if (!write_event(file, stats, current_cycle, "ISSUE", i, command, active_commands)) {
                    std::fclose(file);
                    return false;
                }
            }
        }

        if (stats->completed_commands >= stats->commands) {
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
        if (next_cycle == kInfCycle || next_cycle < current_cycle) {
            std::fclose(file);
            return false;
        }
        if (next_cycle == current_cycle && completed_this_cycle == 0 && issued_this_cycle == 0) {
            std::fclose(file);
            return false;
        }
        current_cycle = next_cycle;
        ++guard;
    }
    if (std::fclose(file) != 0) {
        return false;
    }
    return stats->completed_commands == stats->commands;
}
#endif

[[maybe_unused]] bool write_stats_named(
    const char *path,
    const char *backend_name,
    const Platform &platform,
    const Timing &timing,
    const Stats &stats) {
    FILE *file = std::fopen(path, "w");
    if (file == nullptr) {
        return false;
    }
    std::fprintf(file, "metric,value\n");
    std::fprintf(file, "backend,%s\n", backend_name);
    std::fprintf(file, "platform,%s\n", platform.name.c_str());
    std::fprintf(file, "cycle_ns,%.6f\n", timing.cycle_ns);
    std::fprintf(file, "commands,%d\n", stats.commands);
    std::fprintf(file, "completed_commands,%d\n", stats.completed_commands);
    std::fprintf(file, "events,%d\n", stats.events);
    std::fprintf(file, "issue_events,%d\n", stats.issue_events);
    std::fprintf(file, "complete_events,%d\n", stats.complete_events);
    std::fprintf(file, "max_active_commands,%d\n", stats.max_active_commands);
    std::fprintf(file, "last_event_cycle,%lld\n", stats.last_event_cycle);
    std::fprintf(file, "last_event_ns,%.6f\n", static_cast<double>(stats.last_event_cycle) * timing.cycle_ns);
    std::fprintf(file, "command_address_cycles,%lld\n", timing.command_address_cycles);
    std::fprintf(file, "read_compute_vector_cycles,%lld\n", timing.read_compute_vector_cycles);
    std::fprintf(file, "read_slice_data_cycles,%lld\n", timing.read_slice_data_cycles);
    std::fprintf(file, "array_read_cycles,%lld\n", timing.array_read_cycles);
    std::fprintf(file, "ifc_compute_cycles,%lld\n", timing.ifc_compute_cycles);
    std::fprintf(file, "channels,%d\n", platform.channels);
    std::fprintf(file, "chips_per_channel,%d\n", platform.chips_per_channel);
    std::fprintf(file, "dies_per_chip,%d\n", platform.dies_per_chip);
    std::fprintf(file, "planes_per_die,%d\n", platform.planes_per_die);
    return std::fclose(file) == 0;
}

#ifndef IFC_HW_CYCLE_NO_MAIN
bool write_stats(const char *path, const Platform &platform, const Timing &timing, const Stats &stats) {
    return write_stats_named(path, "cpp_hw_cycle", platform, timing, stats);
}
#endif

long long read_metric_ll(const char *path, const char *metric) {
    FILE *file = std::fopen(path, "r");
    if (file == nullptr) {
        return -1;
    }
    char line[512];
    long long value = -1;
    while (std::fgets(line, sizeof(line), file) != nullptr) {
        char key[128];
        long long parsed_value = -1;
        if (std::sscanf(line, "%127[^,],%lld", key, &parsed_value) == 2 &&
            std::strcmp(key, metric) == 0) {
            value = parsed_value;
            break;
        }
    }
    std::fclose(file);
    return value;
}

bool write_compare_named(const char *path, const char *cycle_label, const char *c_stats_path, const Stats &stats) {
    long long c_events = read_metric_ll(c_stats_path, "events");
    long long c_completed = read_metric_ll(c_stats_path, "completed_commands");
    long long c_last = read_metric_ll(c_stats_path, "last_event_cycle");
    FILE *file = std::fopen(path, "w");
    if (file == nullptr) {
        return false;
    }
    std::fprintf(file, "metric,c_backend,%s,delta,status\n", cycle_label);
    std::fprintf(
        file,
        "events,%lld,%d,%lld,%s\n",
        c_events,
        stats.events,
        c_events >= 0 ? static_cast<long long>(stats.events) - c_events : -1,
        c_events == stats.events ? "PASS" : "NA");
    std::fprintf(
        file,
        "completed_commands,%lld,%d,%lld,%s\n",
        c_completed,
        stats.completed_commands,
        c_completed >= 0 ? static_cast<long long>(stats.completed_commands) - c_completed : -1,
        c_completed == stats.completed_commands ? "PASS" : "NA");
    std::fprintf(
        file,
        "last_event_cycle,%lld,%lld,%lld,%s\n",
        c_last,
        stats.last_event_cycle,
        c_last >= 0 ? stats.last_event_cycle - c_last : -1,
        c_last == stats.last_event_cycle ? "PASS" : "NA");
    return std::fclose(file) == 0;
}

#ifndef IFC_HW_CYCLE_NO_MAIN
bool write_compare(const char *path, const char *c_stats_path, const Stats &stats) {
    return write_compare_named(path, "hw_cycle", c_stats_path, stats);
}
#endif

}  // namespace

#ifndef IFC_HW_CYCLE_NO_MAIN
int main(int argc, char **argv) {
    const char *platform_csv = "configs/default_platforms.csv";
    const char *trace_path = "results/hw_cycle_trace.csv";
    const char *stats_path = "results/hw_cycle_stats.csv";
    const char *compare_path = "results/hw_cycle_compare.csv";
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
            std::fprintf(stderr, "usage: %s [--platforms-csv path] [--trace path] [--stats path] [--compare path] [--c-stats path]\n", argv[0]);
            return 1;
        }
    }

    Platform platform;
    if (!parse_platform_csv(platform_csv, &platform)) {
        std::fprintf(stderr, "error: failed to load platform CSV %s\n", platform_csv);
        return 1;
    }
    if (!platform_supported(platform)) {
        std::fprintf(stderr, "error: first platform dimensions exceed hardware cycle model limits\n");
        return 1;
    }

    Timing timing = derive_timing(platform);
    Stats stats;
    if (!run_event_loop(platform, timing, trace_path, &stats)) {
        std::fprintf(stderr, "error: hardware cycle event loop failed\n");
        return 1;
    }
    if (!write_stats(stats_path, platform, timing, stats)) {
        std::fprintf(stderr, "error: failed to write stats %s\n", stats_path);
        return 1;
    }
    if (!write_compare(compare_path, c_stats_path, stats)) {
        std::fprintf(stderr, "error: failed to write compare %s\n", compare_path);
        return 1;
    }

    std::printf("passed: hardware cycle model\n");
    std::printf("trace_csv: %s\n", trace_path);
    std::printf("stats_csv: %s\n", stats_path);
    std::printf("compare_csv: %s\n", compare_path);
    std::printf("events: %d\n", stats.events);
    std::printf("last_event_cycle: %lld\n", stats.last_event_cycle);
    return 0;
}
#endif
