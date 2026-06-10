#include "ifc_cambricon_llm.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IFC_CONTROLLER_MAX_CHANNELS 64
#define IFC_CONTROLLER_MAX_CHIPS_PER_CHANNEL 128
#define IFC_CONTROLLER_MAX_DIES_PER_CHIP 8
#define IFC_CONTROLLER_MAX_PLANES_PER_DIE 8
#define IFC_CONTROLLER_MAX_CYCLE_COMMANDS 4096
#define IFC_MICRO_STAGE_ISSUE_WIDTH 8
#define IFC_MICRO_ISSUE_FIFO_DEPTH 8
#define IFC_MICRO_MODULE_CLOCK_NS 2.5

typedef struct {
    double channel_busy_until_ns[IFC_CONTROLLER_MAX_CHANNELS];
    double plane_busy_until_ns[IFC_CONTROLLER_MAX_CHANNELS][IFC_CONTROLLER_MAX_CHIPS_PER_CHANNEL]
                                   [IFC_CONTROLLER_MAX_DIES_PER_CHIP][IFC_CONTROLLER_MAX_PLANES_PER_DIE];
    int channels;
    int chips_per_channel;
    int dies_per_chip;
    int planes_per_die;
    int page_bytes;
    double read_ns;
    double program_ns;
    double channel_bandwidth_Bps;
} IfcController;

typedef struct {
    IfcCommandOpcode opcode;
    int logical_id;
    int slice_id;
    int channel;
    int chip;
    int die;
    int plane;
    double issue_ns;
    double channel_start_ns;
    double channel_end_ns;
    double array_start_ns;
    double array_end_ns;
    double complete_ns;
} IfcScheduledCommand;

typedef enum {
    IFC_CYCLE_QUEUED = 0,
    IFC_CYCLE_CHANNEL = 1,
    IFC_CYCLE_WAIT_ARRAY = 2,
    IFC_CYCLE_ARRAY = 3,
    IFC_CYCLE_DONE = 4
} IfcCycleStage;

typedef struct {
    IfcCommandOpcode opcode;
    int logical_id;
    int slice_id;
    int channel;
    int chip;
    int die;
    int plane;
    long long arrival_cycle;
    long long channel_cycles;
    long long array_cycles;
    long long channel_start_cycle;
    long long channel_end_cycle;
    long long array_start_cycle;
    long long array_end_cycle;
    long long complete_cycle;
    long long remaining_cycles;
    IfcCycleStage stage;
} IfcCycleCommand;

typedef struct {
    long long cycle;
    int count;
    int used;
} IfcIssueBucket;

typedef struct {
    IfcIssueBucket *buckets;
    size_t bucket_count;
    long long dispatch_cycles;
    long long issue_events;
    long long dispatch_rounds;
    int issue_width;
    int fifo_depth;
} IfcMicroIssueScheduler;

static double max2(double lhs, double rhs) {
    return lhs > rhs ? lhs : rhs;
}

static long long max_ll(long long lhs, long long rhs) {
    return lhs > rhs ? lhs : rhs;
}

static long long ceil_cycles(double duration_ns, double cycle_ns) {
    long long cycles = (long long)(duration_ns / cycle_ns);
    if ((double)cycles * cycle_ns < duration_ns) {
        ++cycles;
    }
    return cycles > 0 ? cycles : 1;
}

static long long quantize_stage_cycles(long long base_cycles, double cycle_ns, double module_clock_ns) {
    double base_ns;
    double module_cycles;
    double quantized_ns;
    if (base_cycles <= 0) {
        return 1;
    }
    if (module_clock_ns <= 0.0 || cycle_ns <= 0.0) {
        return base_cycles;
    }
    base_ns = (double)base_cycles * cycle_ns;
    module_cycles = ceil(base_ns / module_clock_ns);
    quantized_ns = module_cycles * module_clock_ns;
    return ceil_cycles(quantized_ns, cycle_ns);
}

static size_t next_power_of_two_size(size_t value) {
    size_t result = 1;
    while (result < value) {
        result <<= 1;
    }
    return result;
}

static size_t issue_bucket_hash(long long cycle, size_t mask) {
    unsigned long long value = (unsigned long long)cycle;
    value ^= value >> 33;
    value *= 0xff51afd7ed558ccdULL;
    value ^= value >> 33;
    value *= 0xc4ceb9fe1a85ec53ULL;
    value ^= value >> 33;
    return (size_t)value & mask;
}

static int micro_issue_scheduler_init(
    IfcMicroIssueScheduler *scheduler,
    double cycle_ns,
    long long expected_issue_events) {
    int effective_width = IFC_MICRO_STAGE_ISSUE_WIDTH;
    size_t desired_buckets;
    if (effective_width > IFC_MICRO_ISSUE_FIFO_DEPTH) {
        effective_width = IFC_MICRO_ISSUE_FIFO_DEPTH;
    }
    if (effective_width < 1) {
        effective_width = 1;
    }
    desired_buckets = next_power_of_two_size((size_t)(expected_issue_events * 2 + 1024));
    scheduler->buckets = (IfcIssueBucket *)calloc(desired_buckets, sizeof(IfcIssueBucket));
    if (scheduler->buckets == NULL) {
        return -1;
    }
    scheduler->bucket_count = desired_buckets;
    scheduler->dispatch_cycles = ceil_cycles(IFC_MICRO_MODULE_CLOCK_NS, cycle_ns);
    scheduler->issue_events = 0;
    scheduler->dispatch_rounds = 0;
    scheduler->issue_width = effective_width;
    scheduler->fifo_depth = IFC_MICRO_ISSUE_FIFO_DEPTH;
    return 0;
}

static void micro_issue_scheduler_destroy(IfcMicroIssueScheduler *scheduler) {
    free(scheduler->buckets);
    scheduler->buckets = NULL;
    scheduler->bucket_count = 0;
}

static long long micro_issue_stage(IfcMicroIssueScheduler *scheduler, long long earliest_cycle) {
    long long cycle = earliest_cycle;
    size_t mask = scheduler->bucket_count - 1U;
    for (;;) {
        size_t index = issue_bucket_hash(cycle, mask);
        for (;;) {
            IfcIssueBucket *bucket = &scheduler->buckets[index];
            if (!bucket->used) {
                bucket->used = 1;
                bucket->cycle = cycle;
                bucket->count = 1;
                ++scheduler->issue_events;
                ++scheduler->dispatch_rounds;
                return cycle;
            }
            if (bucket->cycle == cycle) {
                if (bucket->count < scheduler->issue_width) {
                    ++bucket->count;
                    ++scheduler->issue_events;
                    return cycle;
                }
                break;
            }
            index = (index + 1U) & mask;
        }
        cycle += scheduler->dispatch_cycles;
    }
}

static long long round_positive_to_ll(double value) {
    if (value <= 0.0) {
        return 0;
    }
    return (long long)(value + 0.5);
}

static int controller_platform_supported(const IfcPlatformProfile *platform) {
    return platform->channels > 0 &&
           platform->channels <= IFC_CONTROLLER_MAX_CHANNELS &&
           platform->chips_per_channel > 0 &&
           platform->chips_per_channel <= IFC_CONTROLLER_MAX_CHIPS_PER_CHANNEL &&
           platform->dies_per_chip > 0 &&
           platform->dies_per_chip <= IFC_CONTROLLER_MAX_DIES_PER_CHIP &&
           platform->planes_per_die > 0 &&
           platform->planes_per_die <= IFC_CONTROLLER_MAX_PLANES_PER_DIE;
}

static void controller_init(IfcController *controller, const IfcPlatformProfile *platform) {
    memset(controller, 0, sizeof(*controller));
    controller->channels = platform->channels;
    controller->chips_per_channel = platform->chips_per_channel;
    controller->dies_per_chip = platform->dies_per_chip;
    controller->planes_per_die = platform->planes_per_die;
    controller->page_bytes = platform->page_bytes;
    controller->read_ns = platform->array_read_us * 1000.0;
    controller->program_ns = platform->program_us * 1000.0;
    controller->channel_bandwidth_Bps = ifc_platform_channel_bandwidth_Bps(platform);
}

static IfcScheduledCommand controller_submit(
    IfcController *controller,
    IfcCommandOpcode opcode,
    int logical_id,
    int slice_id,
    int channel,
    int chip,
    int die,
    int plane,
    double issue_ns,
    double channel_bytes) {
    IfcScheduledCommand scheduled;
    double channel_service_ns = channel_bytes / controller->channel_bandwidth_Bps * 1e9;
    double array_service_ns = 0.0;
    double *plane_busy = &controller->plane_busy_until_ns[channel][chip][die][plane];

    if (opcode == IFC_OP_READ || opcode == IFC_OP_READ_COMPUTE) {
        array_service_ns = controller->read_ns;
    } else if (opcode == IFC_OP_WRITE) {
        array_service_ns = controller->program_ns;
    }

    scheduled.opcode = opcode;
    scheduled.logical_id = logical_id;
    scheduled.slice_id = slice_id;
    scheduled.channel = channel;
    scheduled.chip = chip;
    scheduled.die = die;
    scheduled.plane = plane;
    scheduled.issue_ns = issue_ns;
    scheduled.channel_start_ns = max2(issue_ns, controller->channel_busy_until_ns[channel]);
    scheduled.channel_end_ns = scheduled.channel_start_ns + channel_service_ns;

    if (opcode == IFC_OP_READ_SLICE) {
        scheduled.array_start_ns = -1.0;
        scheduled.array_end_ns = -1.0;
        scheduled.complete_ns = scheduled.channel_end_ns;
    } else {
        scheduled.array_start_ns = max2(scheduled.channel_end_ns, *plane_busy);
        scheduled.array_end_ns = scheduled.array_start_ns + array_service_ns;
        scheduled.complete_ns = scheduled.array_end_ns;
        *plane_busy = scheduled.array_end_ns;
    }
    controller->channel_busy_until_ns[channel] = scheduled.channel_end_ns;
    return scheduled;
}

static double controller_max_complete_ns(const IfcController *controller) {
    double max_ns = 0.0;
    for (int channel = 0; channel < controller->channels; ++channel) {
        max_ns = max2(max_ns, controller->channel_busy_until_ns[channel]);
        for (int chip = 0; chip < controller->chips_per_channel; ++chip) {
            for (int die = 0; die < controller->dies_per_chip; ++die) {
                for (int plane = 0; plane < controller->planes_per_die; ++plane) {
                    max_ns = max2(max_ns, controller->plane_busy_until_ns[channel][chip][die][plane]);
                }
            }
        }
    }
    return max_ns;
}

int ifc_write_sample_controller_schedule(const char *path) {
    return ifc_write_sample_controller_schedule_for_platform(path, &IFC_PLATFORMS[0]);
}

int ifc_write_sample_controller_schedule_for_platform(const char *path, const IfcPlatformProfile *platform) {
    IfcTileModel tile = ifc_derive_tile_model(platform);
    IfcController controller;
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    if (!controller_platform_supported(platform)) {
        fclose(file);
        return -1;
    }

    controller_init(&controller, platform);
    fprintf(file,
            "sample_model,sample_platform,opcode,logical_id,slice_id,channel,chip,die,plane,"
            "issue_ns,channel_start_ns,channel_end_ns,array_start_ns,array_end_ns,complete_ns\n");
    for (int tile_id = 0; tile_id < IFC_SAMPLE_TILES; ++tile_id) {
        double issue_ns = controller_max_complete_ns(&controller);
        int chip = tile_id % platform->chips_per_channel;
        int die = (tile_id / platform->chips_per_channel) % platform->dies_per_chip;
        int plane = (tile_id / (platform->chips_per_channel * platform->dies_per_chip)) % platform->planes_per_die;
        for (int channel = 0; channel < platform->channels; ++channel) {
            IfcScheduledCommand scheduled = controller_submit(
                &controller,
                IFC_OP_READ_COMPUTE,
                tile_id,
                -1,
                channel,
                chip,
                die,
                plane,
                issue_ns,
                tile.tile_width / (double)platform->channels);
            fprintf(file,
                    "opt_6_7b,%s,%s,%d,%d,%d,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
                    platform->name,
                    ifc_opcode_name(scheduled.opcode),
                    scheduled.logical_id,
                    scheduled.slice_id,
                    scheduled.channel,
                    scheduled.chip,
                    scheduled.die,
                    scheduled.plane,
                    scheduled.issue_ns,
                    scheduled.channel_start_ns,
                    scheduled.channel_end_ns,
                    scheduled.array_start_ns,
                    scheduled.array_end_ns,
                    scheduled.complete_ns);
        }

        if ((tile_id + 1) % IFC_READ_SLICES_PER_REQUEST == 0) {
            double slice_bytes = (double)platform->page_bytes / (double)IFC_READ_SLICES_PER_REQUEST;
            for (int slice_id = 0; slice_id < IFC_READ_SLICES_PER_REQUEST; ++slice_id) {
                for (int channel = 0; channel < platform->channels; ++channel) {
                    int read_plane = (plane + 1) % platform->planes_per_die;
                    IfcScheduledCommand scheduled = controller_submit(
                        &controller,
                        IFC_OP_READ_SLICE,
                        tile_id / IFC_READ_SLICES_PER_REQUEST,
                        slice_id,
                        channel,
                        chip,
                        die,
                        read_plane,
                        issue_ns,
                        slice_bytes);
                    fprintf(file,
                            "opt_6_7b,%s,%s,%d,%d,%d,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
                            platform->name,
                            ifc_opcode_name(scheduled.opcode),
                            scheduled.logical_id,
                            scheduled.slice_id,
                            scheduled.channel,
                            scheduled.chip,
                            scheduled.die,
                            scheduled.plane,
                            scheduled.issue_ns,
                            scheduled.channel_start_ns,
                            scheduled.channel_end_ns,
                            scheduled.array_start_ns,
                            scheduled.array_end_ns,
                            scheduled.complete_ns);
                }
            }
        }
    }
    return fclose(file);
}

static int append_cycle_command(
    IfcCycleCommand commands[],
    int *command_count,
    IfcCommandOpcode opcode,
    int logical_id,
    int slice_id,
    int channel,
    int chip,
    int die,
    int plane,
    long long arrival_cycle,
    long long channel_cycles,
    long long array_cycles) {
    if (*command_count >= IFC_CONTROLLER_MAX_CYCLE_COMMANDS) {
        return -1;
    }
    commands[*command_count].opcode = opcode;
    commands[*command_count].logical_id = logical_id;
    commands[*command_count].slice_id = slice_id;
    commands[*command_count].channel = channel;
    commands[*command_count].chip = chip;
    commands[*command_count].die = die;
    commands[*command_count].plane = plane;
    commands[*command_count].arrival_cycle = arrival_cycle;
    commands[*command_count].channel_cycles = channel_cycles;
    commands[*command_count].array_cycles = array_cycles;
    commands[*command_count].channel_start_cycle = -1;
    commands[*command_count].channel_end_cycle = -1;
    commands[*command_count].array_start_cycle = -1;
    commands[*command_count].array_end_cycle = -1;
    commands[*command_count].complete_cycle = -1;
    commands[*command_count].remaining_cycles = 0;
    commands[*command_count].stage = IFC_CYCLE_QUEUED;
    ++(*command_count);
    return 0;
}

static int build_cycle_commands(
    IfcCycleCommand commands[],
    int *command_count,
    const IfcPlatformProfile *platform,
    double cycle_ns) {
    IfcTileModel tile = ifc_derive_tile_model(platform);
    double channel_bw = ifc_platform_channel_bandwidth_Bps(platform);
    long long array_read_cycles = ceil_cycles(platform->array_read_us * 1000.0, cycle_ns);
    long long read_compute_channel_cycles =
        ceil_cycles(tile.tile_width / (double)platform->channels / channel_bw * 1e9, cycle_ns);
    long long read_slice_channel_cycles =
        ceil_cycles(((double)platform->page_bytes / (double)IFC_READ_SLICES_PER_REQUEST) / channel_bw * 1e9, cycle_ns);

    *command_count = 0;
    for (int tile_id = 0; tile_id < IFC_SAMPLE_TILES; ++tile_id) {
        int chip = tile_id % platform->chips_per_channel;
        int die = (tile_id / platform->chips_per_channel) % platform->dies_per_chip;
        int plane = (tile_id / (platform->chips_per_channel * platform->dies_per_chip)) % platform->planes_per_die;
        for (int channel = 0; channel < platform->channels; ++channel) {
            if (append_cycle_command(
                    commands,
                    command_count,
                    IFC_OP_READ_COMPUTE,
                    tile_id,
                    -1,
                    channel,
                    chip,
                    die,
                    plane,
                    0,
                    read_compute_channel_cycles,
                    array_read_cycles) != 0) {
                return -1;
            }
        }
        if ((tile_id + 1) % IFC_READ_SLICES_PER_REQUEST == 0) {
            int read_plane = (plane + 1) % platform->planes_per_die;
            for (int slice_id = 0; slice_id < IFC_READ_SLICES_PER_REQUEST; ++slice_id) {
                for (int channel = 0; channel < platform->channels; ++channel) {
                    if (append_cycle_command(
                            commands,
                            command_count,
                            IFC_OP_READ_SLICE,
                            tile_id / IFC_READ_SLICES_PER_REQUEST,
                            slice_id,
                            channel,
                            chip,
                            die,
                            read_plane,
                            0,
                            read_slice_channel_cycles,
                            0) != 0) {
                        return -1;
                    }
                }
            }
        }
    }
    return 0;
}

static int simulate_cycle_commands(
    IfcCycleCommand commands[],
    int command_count,
    const IfcPlatformProfile *platform,
    long long *last_cycle) {
    int channel_active[IFC_CONTROLLER_MAX_CHANNELS];
    int plane_active[IFC_CONTROLLER_MAX_CHANNELS][IFC_CONTROLLER_MAX_CHIPS_PER_CHANNEL]
                    [IFC_CONTROLLER_MAX_DIES_PER_CHIP][IFC_CONTROLLER_MAX_PLANES_PER_DIE];
    int done_count = 0;
    long long cycle = 0;
    long long guard_limit = 1000000000LL;

    if (!controller_platform_supported(platform)) {
        return -1;
    }

    for (int channel = 0; channel < IFC_CONTROLLER_MAX_CHANNELS; ++channel) {
        channel_active[channel] = -1;
        for (int chip = 0; chip < IFC_CONTROLLER_MAX_CHIPS_PER_CHANNEL; ++chip) {
            for (int die = 0; die < IFC_CONTROLLER_MAX_DIES_PER_CHIP; ++die) {
                for (int plane = 0; plane < IFC_CONTROLLER_MAX_PLANES_PER_DIE; ++plane) {
                    plane_active[channel][chip][die][plane] = -1;
                }
            }
        }
    }

    while (done_count < command_count && cycle < guard_limit) {
        for (int i = 0; i < command_count; ++i) {
            int channel = commands[i].channel;
            if (commands[i].stage == IFC_CYCLE_QUEUED &&
                commands[i].arrival_cycle <= cycle &&
                channel_active[channel] < 0) {
                commands[i].stage = IFC_CYCLE_CHANNEL;
                commands[i].channel_start_cycle = cycle;
                commands[i].remaining_cycles = commands[i].channel_cycles;
                channel_active[channel] = i;
            }
        }

        for (int i = 0; i < command_count; ++i) {
            int channel = commands[i].channel;
            int chip = commands[i].chip;
            int die = commands[i].die;
            int plane = commands[i].plane;
            if (commands[i].stage == IFC_CYCLE_WAIT_ARRAY &&
                plane_active[channel][chip][die][plane] < 0) {
                commands[i].stage = IFC_CYCLE_ARRAY;
                commands[i].array_start_cycle = cycle;
                commands[i].remaining_cycles = commands[i].array_cycles;
                plane_active[channel][chip][die][plane] = i;
            }
        }

        for (int channel = 0; channel < platform->channels; ++channel) {
            int command_id = channel_active[channel];
            if (command_id >= 0) {
                --commands[command_id].remaining_cycles;
                if (commands[command_id].remaining_cycles <= 0) {
                    commands[command_id].channel_end_cycle = cycle + 1;
                    channel_active[channel] = -1;
                    if (commands[command_id].opcode == IFC_OP_READ_SLICE) {
                        commands[command_id].complete_cycle = cycle + 1;
                        commands[command_id].stage = IFC_CYCLE_DONE;
                        ++done_count;
                    } else {
                        commands[command_id].stage = IFC_CYCLE_WAIT_ARRAY;
                    }
                }
            }
        }

        for (int channel = 0; channel < platform->channels; ++channel) {
            for (int chip = 0; chip < platform->chips_per_channel; ++chip) {
                for (int die = 0; die < platform->dies_per_chip; ++die) {
                    for (int plane = 0; plane < platform->planes_per_die; ++plane) {
                        int command_id = plane_active[channel][chip][die][plane];
                        if (command_id >= 0) {
                            --commands[command_id].remaining_cycles;
                            if (commands[command_id].remaining_cycles <= 0) {
                                commands[command_id].array_end_cycle = cycle + 1;
                                commands[command_id].complete_cycle = cycle + 1;
                                commands[command_id].stage = IFC_CYCLE_DONE;
                                plane_active[channel][chip][die][plane] = -1;
                                ++done_count;
                            }
                        }
                    }
                }
            }
        }

        ++cycle;
    }

    *last_cycle = cycle;
    return done_count == command_count ? 0 : -1;
}

static int write_cycle_trace_file(
    const char *trace_path,
    const IfcCycleCommand commands[],
    int command_count,
    double cycle_ns) {
    FILE *file = fopen(trace_path, "w");
    if (file == NULL) {
        return -1;
    }
    fprintf(file,
            "command_id,opcode,logical_id,slice_id,channel,chip,die,plane,arrival_cycle,"
            "channel_start_cycle,channel_end_cycle,array_start_cycle,array_end_cycle,complete_cycle,"
            "channel_cycles,array_cycles,complete_ns\n");
    for (int i = 0; i < command_count; ++i) {
        fprintf(file,
                "%d,%s,%d,%d,%d,%d,%d,%d,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%.3f\n",
                i,
                ifc_opcode_name(commands[i].opcode),
                commands[i].logical_id,
                commands[i].slice_id,
                commands[i].channel,
                commands[i].chip,
                commands[i].die,
                commands[i].plane,
                commands[i].arrival_cycle,
                commands[i].channel_start_cycle,
                commands[i].channel_end_cycle,
                commands[i].array_start_cycle,
                commands[i].array_end_cycle,
                commands[i].complete_cycle,
                commands[i].channel_cycles,
                commands[i].array_cycles,
                (double)commands[i].complete_cycle * cycle_ns);
    }
    return fclose(file);
}

static int write_cycle_stats_file(
    const char *stats_path,
    const IfcCycleCommand commands[],
    int command_count,
    const IfcPlatformProfile *platform,
    double cycle_ns,
    long long last_cycle) {
    FILE *file = fopen(stats_path, "w");
    int read_compute_count = 0;
    int read_slice_count = 0;
    long long read_compute_channel_cycles = 0;
    long long read_slice_channel_cycles = 0;
    long long array_cycles = 0;
    if (file == NULL) {
        return -1;
    }
    for (int i = 0; i < command_count; ++i) {
        if (commands[i].opcode == IFC_OP_READ_COMPUTE) {
            ++read_compute_count;
            read_compute_channel_cycles += commands[i].channel_cycles;
            array_cycles += commands[i].array_cycles;
        } else if (commands[i].opcode == IFC_OP_READ_SLICE) {
            ++read_slice_count;
            read_slice_channel_cycles += commands[i].channel_cycles;
        }
    }
    fprintf(file, "metric,value\n");
    fprintf(file, "cycle_ns,%.6f\n", cycle_ns);
    fprintf(file, "last_cycle,%lld\n", last_cycle);
    fprintf(file, "last_cycle_ns,%.6f\n", (double)last_cycle * cycle_ns);
    fprintf(file, "commands,%d\n", command_count);
    fprintf(file, "read_compute_commands,%d\n", read_compute_count);
    fprintf(file, "read_slice_commands,%d\n", read_slice_count);
    fprintf(file, "channels,%d\n", platform->channels);
    fprintf(file, "chips_per_channel,%d\n", platform->chips_per_channel);
    fprintf(file, "dies_per_chip,%d\n", platform->dies_per_chip);
    fprintf(file, "planes_per_die,%d\n", platform->planes_per_die);
    fprintf(file, "read_compute_channel_cycles,%lld\n", read_compute_channel_cycles);
    fprintf(file, "read_slice_channel_cycles,%lld\n", read_slice_channel_cycles);
    fprintf(file, "array_cycles,%lld\n", array_cycles);
    return fclose(file);
}

static void cycle_weight_place(
    const IfcPlatformProfile *platform,
    long long index,
    int plane_offset,
    int *channel,
    int *chip,
    int *die,
    int *plane) {
    long long lane;
    long long group;
    *channel = (int)(index % platform->channels);
    group = index / platform->channels;
    lane = group % ((long long)platform->chips_per_channel * (long long)platform->dies_per_chip);
    *chip = (int)(lane % platform->chips_per_channel);
    *die = (int)((lane / platform->chips_per_channel) % platform->dies_per_chip);
    *plane = (int)(((group / ((long long)platform->chips_per_channel * (long long)platform->dies_per_chip)) +
                    (long long)plane_offset) %
                   platform->planes_per_die);
}

static void cycle_weight_submit_read_compute(
    const IfcPlatformProfile *platform,
    long long index,
    long long command_address_cycles,
    long long vector_cycles,
    long long array_read_cycles,
    long long ifc_compute_cycles,
    long long channel_ready[IFC_CONTROLLER_MAX_CHANNELS],
    long long chip_ready[IFC_CONTROLLER_MAX_CHANNELS][IFC_CONTROLLER_MAX_CHIPS_PER_CHANNEL],
    long long plane_ready[IFC_CONTROLLER_MAX_CHANNELS][IFC_CONTROLLER_MAX_CHIPS_PER_CHANNEL]
                         [IFC_CONTROLLER_MAX_DIES_PER_CHIP][IFC_CONTROLLER_MAX_PLANES_PER_DIE],
    long long compute_ready[IFC_CONTROLLER_MAX_CHANNELS][IFC_CONTROLLER_MAX_CHIPS_PER_CHANNEL]
                           [IFC_CONTROLLER_MAX_DIES_PER_CHIP],
    IfcMicroIssueScheduler *issue_scheduler,
    long long *last_cycle) {
    int channel;
    int chip;
    int die;
    int plane;
    long long start;
    long long end;

    cycle_weight_place(platform, index, 0, &channel, &chip, &die, &plane);
    start = micro_issue_stage(issue_scheduler, max_ll(channel_ready[channel], chip_ready[channel][chip]));
    end = start + command_address_cycles;
    channel_ready[channel] = end;
    chip_ready[channel][chip] = end;

    start = micro_issue_stage(issue_scheduler, max_ll(channel_ready[channel], chip_ready[channel][chip]));
    end = start + vector_cycles;
    channel_ready[channel] = end;
    chip_ready[channel][chip] = end;

    start = micro_issue_stage(issue_scheduler, max_ll(chip_ready[channel][chip], plane_ready[channel][chip][die][plane]));
    end = start + array_read_cycles;
    chip_ready[channel][chip] = end;
    plane_ready[channel][chip][die][plane] = end;

    start = micro_issue_stage(
        issue_scheduler,
        max_ll(max_ll(chip_ready[channel][chip], plane_ready[channel][chip][die][plane]),
               compute_ready[channel][chip][die]));
    end = start + ifc_compute_cycles;
    chip_ready[channel][chip] = end;
    plane_ready[channel][chip][die][plane] = end;
    compute_ready[channel][chip][die] = end;
    if (end > *last_cycle) {
        *last_cycle = end;
    }
}

static void cycle_weight_submit_read_slice(
    const IfcPlatformProfile *platform,
    long long index,
    long long command_address_cycles,
    long long data_cycles,
    long long channel_ready[IFC_CONTROLLER_MAX_CHANNELS],
    long long chip_ready[IFC_CONTROLLER_MAX_CHANNELS][IFC_CONTROLLER_MAX_CHIPS_PER_CHANNEL],
    IfcMicroIssueScheduler *issue_scheduler,
    long long *last_cycle) {
    int channel;
    int chip;
    int die;
    int plane;
    long long start;
    long long end;

    cycle_weight_place(platform, index, 1, &channel, &chip, &die, &plane);
    (void)die;
    (void)plane;
    start = micro_issue_stage(issue_scheduler, max_ll(channel_ready[channel], chip_ready[channel][chip]));
    end = start + command_address_cycles;
    channel_ready[channel] = end;
    chip_ready[channel][chip] = end;

    start = micro_issue_stage(issue_scheduler, max_ll(channel_ready[channel], chip_ready[channel][chip]));
    end = start + data_cycles;
    channel_ready[channel] = end;
    chip_ready[channel][chip] = end;
    if (end > *last_cycle) {
        *last_cycle = end;
    }
}

int ifc_estimate_cycle_weight_stage(
    const IfcPlatformProfile *platform,
    double read_compute_requests,
    double read_slice_commands,
    double effective_efficiency,
    IfcCycleWeightStats *stats) {
    IfcTileModel tile;
    long long channel_ready[IFC_CONTROLLER_MAX_CHANNELS];
    long long chip_ready[IFC_CONTROLLER_MAX_CHANNELS][IFC_CONTROLLER_MAX_CHIPS_PER_CHANNEL];
    long long plane_ready[IFC_CONTROLLER_MAX_CHANNELS][IFC_CONTROLLER_MAX_CHIPS_PER_CHANNEL]
                         [IFC_CONTROLLER_MAX_DIES_PER_CHIP][IFC_CONTROLLER_MAX_PLANES_PER_DIE];
    long long compute_ready[IFC_CONTROLLER_MAX_CHANNELS][IFC_CONTROLLER_MAX_CHIPS_PER_CHANNEL]
                           [IFC_CONTROLLER_MAX_DIES_PER_CHIP];
    IfcMicroIssueScheduler issue_scheduler;
    long long read_compute_count;
    long long read_slice_count;
    long long slice_done = 0;
    long long last_cycle = 0;
    long long command_multiplier;
    double cycle_ns;
    double onfi_cycle_ns;
    double channel_bw;
    double ops_per_read_compute;
    double compute_ops_per_s;
    long long command_address_cycles;
    long long vector_cycles;
    long long data_cycles;
    long long array_read_cycles;
    long long ifc_compute_cycles;

    if (stats == NULL || !controller_platform_supported(platform) || effective_efficiency <= 0.0) {
        return -1;
    }

    memset(stats, 0, sizeof(*stats));
    memset(channel_ready, 0, sizeof(channel_ready));
    memset(chip_ready, 0, sizeof(chip_ready));
    memset(plane_ready, 0, sizeof(plane_ready));
    memset(compute_ready, 0, sizeof(compute_ready));

    tile = ifc_derive_tile_model(platform);
    cycle_ns = platform->ifc_frequency_hz > 0.0 ? 1e9 / platform->ifc_frequency_hz : 1.0;
    onfi_cycle_ns = platform->onfi_rate_MTps > 0.0 ? 1000.0 / platform->onfi_rate_MTps : cycle_ns;
    channel_bw = ifc_platform_channel_bandwidth_Bps(platform);
    ops_per_read_compute = tile.tile_height * (tile.tile_width / (double)platform->channels);
    compute_ops_per_s =
        (double)platform->compute_cores_per_die * platform->ifc_frequency_hz * platform->ifc_ops_per_core_cycle;

    command_address_cycles = ceil_cycles(7.0 * onfi_cycle_ns, cycle_ns);
    vector_cycles = ceil_cycles(tile.tile_width / (double)platform->channels / channel_bw * 1e9, cycle_ns);
    data_cycles = ceil_cycles(((double)platform->page_bytes / (double)IFC_READ_SLICES_PER_REQUEST) / channel_bw * 1e9, cycle_ns);
    array_read_cycles = ceil_cycles(platform->array_read_us * 1000.0, cycle_ns);
    ifc_compute_cycles =
        compute_ops_per_s > 0.0 ? ceil_cycles(ops_per_read_compute / compute_ops_per_s * 1e9, cycle_ns) : 1;
    command_address_cycles = quantize_stage_cycles(command_address_cycles, cycle_ns, IFC_MICRO_MODULE_CLOCK_NS);
    vector_cycles = quantize_stage_cycles(vector_cycles, cycle_ns, IFC_MICRO_MODULE_CLOCK_NS);
    data_cycles = quantize_stage_cycles(data_cycles, cycle_ns, IFC_MICRO_MODULE_CLOCK_NS);
    array_read_cycles = quantize_stage_cycles(array_read_cycles, cycle_ns, IFC_MICRO_MODULE_CLOCK_NS);
    ifc_compute_cycles = quantize_stage_cycles(ifc_compute_cycles, cycle_ns, IFC_MICRO_MODULE_CLOCK_NS);

    command_multiplier = (long long)platform->chips_per_channel * (long long)platform->dies_per_chip;
    if (command_multiplier < 1) {
        command_multiplier = 1;
    }
    read_compute_count = round_positive_to_ll(read_compute_requests * (double)command_multiplier);
    read_slice_count = round_positive_to_ll(read_slice_commands * (double)command_multiplier);
    if (read_compute_count <= 0 && read_slice_count <= 0) {
        return -1;
    }
    if (micro_issue_scheduler_init(
            &issue_scheduler,
            cycle_ns,
            read_compute_count * 4LL + read_slice_count * 2LL) != 0) {
        return -1;
    }

    for (long long rc = 0; rc < read_compute_count; ++rc) {
        long long target_slices;
        cycle_weight_submit_read_compute(
            platform,
            rc,
            command_address_cycles,
            vector_cycles,
            array_read_cycles,
            ifc_compute_cycles,
            channel_ready,
            chip_ready,
            plane_ready,
            compute_ready,
            &issue_scheduler,
            &last_cycle);
        target_slices = ((rc + 1) * read_slice_count + read_compute_count / 2) / read_compute_count;
        while (slice_done < target_slices) {
            cycle_weight_submit_read_slice(
                platform,
                slice_done,
                command_address_cycles,
                data_cycles,
                channel_ready,
                chip_ready,
                &issue_scheduler,
                &last_cycle);
            ++slice_done;
        }
    }
    while (slice_done < read_slice_count) {
        cycle_weight_submit_read_slice(
            platform,
            slice_done,
            command_address_cycles,
            data_cycles,
            channel_ready,
            chip_ready,
            &issue_scheduler,
            &last_cycle);
        ++slice_done;
    }

    stats->last_cycle = last_cycle;
    stats->read_compute_commands = read_compute_count;
    stats->read_slice_commands = read_slice_count;
    stats->total_commands = read_compute_count + read_slice_count;
    stats->command_multiplier = (int)command_multiplier;
    stats->cycle_ns = cycle_ns;
    stats->raw_weight_stage_ms = (double)last_cycle * cycle_ns * 1e-6;
    stats->calibrated_weight_stage_ms = stats->raw_weight_stage_ms / effective_efficiency;
    stats->command_address_cycles = (double)command_address_cycles;
    stats->read_compute_vector_cycles = (double)vector_cycles;
    stats->read_slice_data_cycles = (double)data_cycles;
    stats->array_read_cycles = (double)array_read_cycles;
    stats->ifc_compute_cycles = (double)ifc_compute_cycles;
    stats->issue_width = issue_scheduler.issue_width;
    stats->issue_fifo_depth = issue_scheduler.fifo_depth;
    stats->module_clock_ns = IFC_MICRO_MODULE_CLOCK_NS;
    stats->stage_issue_events = issue_scheduler.issue_events;
    stats->dispatch_rounds = issue_scheduler.dispatch_rounds;
    micro_issue_scheduler_destroy(&issue_scheduler);
    return 0;
}

int ifc_write_cycle_controller_trace_for_platform(
    const char *trace_path,
    const char *stats_path,
    const IfcPlatformProfile *platform) {
    IfcCycleCommand commands[IFC_CONTROLLER_MAX_CYCLE_COMMANDS];
    int command_count = 0;
    long long last_cycle = 0;
    double cycle_ns = platform->ifc_frequency_hz > 0.0 ? 1e9 / platform->ifc_frequency_hz : 1.0;

    if (!controller_platform_supported(platform)) {
        return -1;
    }
    if (build_cycle_commands(commands, &command_count, platform, cycle_ns) != 0) {
        return -1;
    }
    if (simulate_cycle_commands(commands, command_count, platform, &last_cycle) != 0) {
        return -1;
    }
    if (write_cycle_trace_file(trace_path, commands, command_count, cycle_ns) != 0) {
        return -1;
    }
    if (write_cycle_stats_file(stats_path, commands, command_count, platform, cycle_ns, last_cycle) != 0) {
        return -1;
    }
    return 0;
}
