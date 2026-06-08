#include "ifc_cambricon_llm.h"

#include <stdio.h>
#include <string.h>

#define IFC_SSDSIM_MAX_CHANNELS 64
#define IFC_SSDSIM_MAX_CHIPS_PER_CHANNEL 128
#define IFC_SSDSIM_MAX_DIES_PER_CHIP 8
#define IFC_SSDSIM_MAX_PLANES_PER_DIE 8
#define IFC_SSDSIM_MAX_COMMANDS 4096
#define IFC_SSDSIM_INF_CYCLE 0x3fffffffffffffffLL

typedef struct {
    long long channel_ready[IFC_SSDSIM_MAX_CHANNELS];
    long long chip_ready[IFC_SSDSIM_MAX_CHANNELS][IFC_SSDSIM_MAX_CHIPS_PER_CHANNEL];
    long long plane_ready[IFC_SSDSIM_MAX_CHANNELS][IFC_SSDSIM_MAX_CHIPS_PER_CHANNEL]
                         [IFC_SSDSIM_MAX_DIES_PER_CHIP][IFC_SSDSIM_MAX_PLANES_PER_DIE];
    long long compute_ready[IFC_SSDSIM_MAX_CHANNELS][IFC_SSDSIM_MAX_CHIPS_PER_CHANNEL]
                           [IFC_SSDSIM_MAX_DIES_PER_CHIP];
    int stage_events;
    int read_compute_commands;
    int read_slice_commands;
    long long last_complete_cycle;
} IfcSsdSimBackend;

typedef struct {
    double cycle_ns;
    long long command_address_cycles;
    long long read_compute_vector_cycles;
    long long read_slice_data_cycles;
    long long array_read_cycles;
    long long ifc_compute_cycles;
} IfcSsdSimTiming;

typedef enum {
    IFC_EVENT_WAITING = 0,
    IFC_EVENT_ACTIVE = 1,
    IFC_EVENT_DONE = 2
} IfcEventCommandStatus;

typedef struct {
    IfcCommandOpcode opcode;
    int logical_id;
    int slice_id;
    int channel;
    int chip;
    int die;
    int plane;
    int stage_index;
    IfcEventCommandStatus status;
    long long arrival_cycle;
    long long stage_start_cycle;
    long long stage_end_cycle;
    long long stage_duration_cycles;
} IfcEventCommand;

typedef struct {
    int channel_active[IFC_SSDSIM_MAX_CHANNELS];
    int chip_active[IFC_SSDSIM_MAX_CHANNELS][IFC_SSDSIM_MAX_CHIPS_PER_CHANNEL];
    int plane_active[IFC_SSDSIM_MAX_CHANNELS][IFC_SSDSIM_MAX_CHIPS_PER_CHANNEL]
                    [IFC_SSDSIM_MAX_DIES_PER_CHIP][IFC_SSDSIM_MAX_PLANES_PER_DIE];
    int compute_active[IFC_SSDSIM_MAX_CHANNELS][IFC_SSDSIM_MAX_CHIPS_PER_CHANNEL]
                      [IFC_SSDSIM_MAX_DIES_PER_CHIP];
    int events;
    int issue_events;
    int complete_events;
    int max_active_commands;
    int completed_commands;
    long long last_event_cycle;
} IfcEventBackend;

static long long max_ll(long long lhs, long long rhs) {
    return lhs > rhs ? lhs : rhs;
}

static long long ceil_cycles_ssd(double duration_ns, double cycle_ns) {
    long long cycles = (long long)(duration_ns / cycle_ns);
    if ((double)cycles * cycle_ns < duration_ns) {
        ++cycles;
    }
    return cycles > 0 ? cycles : 1;
}

static int ssdsim_platform_supported(const IfcPlatformProfile *platform) {
    return platform->channels > 0 &&
           platform->channels <= IFC_SSDSIM_MAX_CHANNELS &&
           platform->chips_per_channel > 0 &&
           platform->chips_per_channel <= IFC_SSDSIM_MAX_CHIPS_PER_CHANNEL &&
           platform->dies_per_chip > 0 &&
           platform->dies_per_chip <= IFC_SSDSIM_MAX_DIES_PER_CHIP &&
           platform->planes_per_die > 0 &&
           platform->planes_per_die <= IFC_SSDSIM_MAX_PLANES_PER_DIE;
}

static void ssdsim_backend_init(IfcSsdSimBackend *backend) {
    memset(backend, 0, sizeof(*backend));
}

static void event_backend_init(IfcEventBackend *backend) {
    for (int channel = 0; channel < IFC_SSDSIM_MAX_CHANNELS; ++channel) {
        backend->channel_active[channel] = -1;
        for (int chip = 0; chip < IFC_SSDSIM_MAX_CHIPS_PER_CHANNEL; ++chip) {
            backend->chip_active[channel][chip] = -1;
            for (int die = 0; die < IFC_SSDSIM_MAX_DIES_PER_CHIP; ++die) {
                backend->compute_active[channel][chip][die] = -1;
                for (int plane = 0; plane < IFC_SSDSIM_MAX_PLANES_PER_DIE; ++plane) {
                    backend->plane_active[channel][chip][die][plane] = -1;
                }
            }
        }
    }
    backend->events = 0;
    backend->issue_events = 0;
    backend->complete_events = 0;
    backend->max_active_commands = 0;
    backend->completed_commands = 0;
    backend->last_event_cycle = 0;
}

static IfcSsdSimTiming ssdsim_derive_timing(const IfcPlatformProfile *platform) {
    IfcTileModel tile = ifc_derive_tile_model(platform);
    IfcSsdSimTiming timing;
    double channel_bw = ifc_platform_channel_bandwidth_Bps(platform);
    double cycle_ns = platform->ifc_frequency_hz > 0.0 ? 1e9 / platform->ifc_frequency_hz : 1.0;
    double onfi_cycle_ns = platform->onfi_rate_MTps > 0.0 ? 1000.0 / platform->onfi_rate_MTps : cycle_ns;
    double ops_per_read_compute =
        tile.tile_height * (tile.tile_width / (double)platform->channels);
    double compute_ops_per_s =
        (double)platform->compute_cores_per_die * platform->ifc_frequency_hz * platform->ifc_ops_per_core_cycle;

    timing.cycle_ns = cycle_ns;
    timing.command_address_cycles = ceil_cycles_ssd(7.0 * onfi_cycle_ns, cycle_ns);
    timing.read_compute_vector_cycles =
        ceil_cycles_ssd(tile.tile_width / (double)platform->channels / channel_bw * 1e9, cycle_ns);
    timing.read_slice_data_cycles =
        ceil_cycles_ssd(((double)platform->page_bytes / (double)IFC_READ_SLICES_PER_REQUEST) / channel_bw * 1e9, cycle_ns);
    timing.array_read_cycles = ceil_cycles_ssd(platform->array_read_us * 1000.0, cycle_ns);
    timing.ifc_compute_cycles =
        compute_ops_per_s > 0.0 ? ceil_cycles_ssd(ops_per_read_compute / compute_ops_per_s * 1e9, cycle_ns) : 1;
    return timing;
}

static int write_stage(
    FILE *file,
    IfcSsdSimBackend *backend,
    int command_id,
    IfcCommandOpcode opcode,
    int logical_id,
    int slice_id,
    int channel,
    int chip,
    int die,
    int plane,
    const char *stage,
    const char *subrequest_state,
    const char *channel_state,
    const char *chip_state,
    const char *plane_state,
    long long start_cycle,
    long long duration_cycles) {
    long long end_cycle = start_cycle + duration_cycles;
    if (fprintf(
            file,
            "%d,%s,%d,%d,%d,%d,%d,%d,%s,%s,%s,%s,%s,%lld,%lld,%lld\n",
            command_id,
            ifc_opcode_name(opcode),
            logical_id,
            slice_id,
            channel,
            chip,
            die,
            plane,
            stage,
            subrequest_state,
            channel_state,
            chip_state,
            plane_state,
            start_cycle,
            end_cycle,
            duration_cycles) < 0) {
        return -1;
    }
    ++backend->stage_events;
    if (end_cycle > backend->last_complete_cycle) {
        backend->last_complete_cycle = end_cycle;
    }
    return 0;
}

static int submit_read_compute(
    FILE *file,
    IfcSsdSimBackend *backend,
    const IfcSsdSimTiming *timing,
    int command_id,
    int logical_id,
    int channel,
    int chip,
    int die,
    int plane) {
    long long ca_start = max_ll(backend->channel_ready[channel], backend->chip_ready[channel][chip]);
    long long ca_end = ca_start + timing->command_address_cycles;
    long long vector_start;
    long long vector_end;
    long long array_start;
    long long array_end;
    long long compute_start;
    long long compute_end;

    if (write_stage(
            file,
            backend,
            command_id,
            IFC_OP_READ_COMPUTE,
            logical_id,
            -1,
            channel,
            chip,
            die,
            plane,
            "SSDSIM_CA_TRANSFER",
            "SR_R_C_A_TRANSFER",
            "CHANNEL_C_A_TRANSFER",
            "CHIP_C_A_TRANSFER",
            "PLANE_CMD_LATCH",
            ca_start,
            timing->command_address_cycles) != 0) {
        return -1;
    }
    backend->channel_ready[channel] = ca_end;
    backend->chip_ready[channel][chip] = ca_end;

    vector_start = max_ll(backend->channel_ready[channel], backend->chip_ready[channel][chip]);
    vector_end = vector_start + timing->read_compute_vector_cycles;
    if (write_stage(
            file,
            backend,
            command_id,
            IFC_OP_READ_COMPUTE,
            logical_id,
            -1,
            channel,
            chip,
            die,
            plane,
            "IFC_VECTOR_TRANSFER",
            "SR_IFC_VECTOR_TRANSFER",
            "CHANNEL_DATA_TRANSFER",
            "CHIP_DATA_TRANSFER",
            "PLANE_INPUT_LATCH",
            vector_start,
            timing->read_compute_vector_cycles) != 0) {
        return -1;
    }
    backend->channel_ready[channel] = vector_end;
    backend->chip_ready[channel][chip] = vector_end;

    array_start = max_ll(backend->chip_ready[channel][chip], backend->plane_ready[channel][chip][die][plane]);
    array_end = array_start + timing->array_read_cycles;
    if (write_stage(
            file,
            backend,
            command_id,
            IFC_OP_READ_COMPUTE,
            logical_id,
            -1,
            channel,
            chip,
            die,
            plane,
            "SSDSIM_ARRAY_READ",
            "SR_R_READ",
            "CHANNEL_IDLE",
            "CHIP_READ_BUSY",
            "PLANE_ARRAY_BUSY",
            array_start,
            timing->array_read_cycles) != 0) {
        return -1;
    }
    backend->chip_ready[channel][chip] = array_end;
    backend->plane_ready[channel][chip][die][plane] = array_end;

    compute_start = max_ll(array_end, backend->compute_ready[channel][chip][die]);
    compute_end = compute_start + timing->ifc_compute_cycles;
    if (write_stage(
            file,
            backend,
            command_id,
            IFC_OP_READ_COMPUTE,
            logical_id,
            -1,
            channel,
            chip,
            die,
            plane,
            "IFC_COMPUTE",
            "SR_IFC_COMPUTE",
            "CHANNEL_IDLE",
            "CHIP_IFC_COMPUTE",
            "PLANE_IFC_COMPUTE",
            compute_start,
            timing->ifc_compute_cycles) != 0) {
        return -1;
    }
    backend->compute_ready[channel][chip][die] = compute_end;
    backend->chip_ready[channel][chip] = compute_end;
    backend->plane_ready[channel][chip][die][plane] = compute_end;
    ++backend->read_compute_commands;
    return 0;
}

static int submit_read_slice(
    FILE *file,
    IfcSsdSimBackend *backend,
    const IfcSsdSimTiming *timing,
    int command_id,
    int logical_id,
    int slice_id,
    int channel,
    int chip,
    int die,
    int plane) {
    long long ca_start = max_ll(backend->channel_ready[channel], backend->chip_ready[channel][chip]);
    long long ca_end = ca_start + timing->command_address_cycles;
    long long data_start;
    long long data_end;

    if (write_stage(
            file,
            backend,
            command_id,
            IFC_OP_READ_SLICE,
            logical_id,
            slice_id,
            channel,
            chip,
            die,
            plane,
            "SSDSIM_CA_TRANSFER",
            "SR_R_C_A_TRANSFER",
            "CHANNEL_C_A_TRANSFER",
            "CHIP_C_A_TRANSFER",
            "PLANE_CMD_LATCH",
            ca_start,
            timing->command_address_cycles) != 0) {
        return -1;
    }
    backend->channel_ready[channel] = ca_end;
    backend->chip_ready[channel][chip] = ca_end;

    data_start = max_ll(backend->channel_ready[channel], backend->chip_ready[channel][chip]);
    data_end = data_start + timing->read_slice_data_cycles;
    if (write_stage(
            file,
            backend,
            command_id,
            IFC_OP_READ_SLICE,
            logical_id,
            slice_id,
            channel,
            chip,
            die,
            plane,
            "SSDSIM_DATA_TRANSFER",
            "SR_R_DATA_TRANSFER",
            "CHANNEL_DATA_TRANSFER",
            "CHIP_DATA_TRANSFER",
            "PLANE_DATA_REGISTER",
            data_start,
            timing->read_slice_data_cycles) != 0) {
        return -1;
    }
    backend->channel_ready[channel] = data_end;
    backend->chip_ready[channel][chip] = data_end;
    ++backend->read_slice_commands;
    return 0;
}

static int write_stats_file(
    const char *path,
    const IfcPlatformProfile *platform,
    const IfcSsdSimBackend *backend,
    const IfcSsdSimTiming *timing) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    fprintf(file, "metric,value\n");
    fprintf(file, "cycle_ns,%.6f\n", timing->cycle_ns);
    fprintf(file, "command_address_cycles,%lld\n", timing->command_address_cycles);
    fprintf(file, "read_compute_vector_cycles,%lld\n", timing->read_compute_vector_cycles);
    fprintf(file, "read_slice_data_cycles,%lld\n", timing->read_slice_data_cycles);
    fprintf(file, "array_read_cycles,%lld\n", timing->array_read_cycles);
    fprintf(file, "ifc_compute_cycles,%lld\n", timing->ifc_compute_cycles);
    fprintf(file, "stage_events,%d\n", backend->stage_events);
    fprintf(file, "commands,%d\n", backend->read_compute_commands + backend->read_slice_commands);
    fprintf(file, "read_compute_commands,%d\n", backend->read_compute_commands);
    fprintf(file, "read_slice_commands,%d\n", backend->read_slice_commands);
    fprintf(file, "last_complete_cycle,%lld\n", backend->last_complete_cycle);
    fprintf(file, "last_complete_ns,%.6f\n", (double)backend->last_complete_cycle * timing->cycle_ns);
    fprintf(file, "channels,%d\n", platform->channels);
    fprintf(file, "chips_per_channel,%d\n", platform->chips_per_channel);
    fprintf(file, "dies_per_chip,%d\n", platform->dies_per_chip);
    fprintf(file, "planes_per_die,%d\n", platform->planes_per_die);
    return fclose(file);
}

int ifc_write_ssdsim_ifc_trace_for_platform(
    const char *trace_path,
    const char *stats_path,
    const IfcPlatformProfile *platform) {
    FILE *file;
    IfcSsdSimBackend backend;
    IfcSsdSimTiming timing;
    int command_id = 0;

    if (!ssdsim_platform_supported(platform)) {
        return -1;
    }

    file = fopen(trace_path, "w");
    if (file == NULL) {
        return -1;
    }

    ssdsim_backend_init(&backend);
    timing = ssdsim_derive_timing(platform);

    fprintf(file,
            "command_id,opcode,logical_id,slice_id,channel,chip,die,plane,stage,subrequest_state,"
            "channel_state,chip_state,plane_state,start_cycle,end_cycle,duration_cycles\n");

    for (int tile_id = 0; tile_id < IFC_SAMPLE_TILES; ++tile_id) {
        int chip = tile_id % platform->chips_per_channel;
        int die = (tile_id / platform->chips_per_channel) % platform->dies_per_chip;
        int plane = (tile_id / (platform->chips_per_channel * platform->dies_per_chip)) % platform->planes_per_die;
        for (int channel = 0; channel < platform->channels; ++channel) {
            if (submit_read_compute(
                    file,
                    &backend,
                    &timing,
                    command_id,
                    tile_id,
                    channel,
                    chip,
                    die,
                    plane) != 0) {
                fclose(file);
                return -1;
            }
            ++command_id;
        }
        if ((tile_id + 1) % IFC_READ_SLICES_PER_REQUEST == 0) {
            int read_plane = (plane + 1) % platform->planes_per_die;
            for (int slice_id = 0; slice_id < IFC_READ_SLICES_PER_REQUEST; ++slice_id) {
                for (int channel = 0; channel < platform->channels; ++channel) {
                    if (submit_read_slice(
                            file,
                            &backend,
                            &timing,
                            command_id,
                            tile_id / IFC_READ_SLICES_PER_REQUEST,
                            slice_id,
                            channel,
                            chip,
                            die,
                            read_plane) != 0) {
                        fclose(file);
                        return -1;
                    }
                    ++command_id;
                }
            }
        }
    }

    if (fclose(file) != 0) {
        return -1;
    }
    return write_stats_file(stats_path, platform, &backend, &timing);
}

static int event_append_command(
    IfcEventCommand commands[],
    int *command_count,
    IfcCommandOpcode opcode,
    int logical_id,
    int slice_id,
    int channel,
    int chip,
    int die,
    int plane) {
    if (*command_count >= IFC_SSDSIM_MAX_COMMANDS) {
        return -1;
    }
    commands[*command_count].opcode = opcode;
    commands[*command_count].logical_id = logical_id;
    commands[*command_count].slice_id = slice_id;
    commands[*command_count].channel = channel;
    commands[*command_count].chip = chip;
    commands[*command_count].die = die;
    commands[*command_count].plane = plane;
    commands[*command_count].stage_index = 0;
    commands[*command_count].status = IFC_EVENT_WAITING;
    commands[*command_count].arrival_cycle = 0;
    commands[*command_count].stage_start_cycle = -1;
    commands[*command_count].stage_end_cycle = -1;
    commands[*command_count].stage_duration_cycles = 0;
    ++(*command_count);
    return 0;
}

static int event_build_commands(IfcEventCommand commands[], int *command_count, const IfcPlatformProfile *platform) {
    *command_count = 0;
    for (int tile_id = 0; tile_id < IFC_SAMPLE_TILES; ++tile_id) {
        int chip = tile_id % platform->chips_per_channel;
        int die = (tile_id / platform->chips_per_channel) % platform->dies_per_chip;
        int plane = (tile_id / (platform->chips_per_channel * platform->dies_per_chip)) % platform->planes_per_die;
        for (int channel = 0; channel < platform->channels; ++channel) {
            if (event_append_command(
                    commands,
                    command_count,
                    IFC_OP_READ_COMPUTE,
                    tile_id,
                    -1,
                    channel,
                    chip,
                    die,
                    plane) != 0) {
                return -1;
            }
        }
        if ((tile_id + 1) % IFC_READ_SLICES_PER_REQUEST == 0) {
            int read_plane = (plane + 1) % platform->planes_per_die;
            for (int slice_id = 0; slice_id < IFC_READ_SLICES_PER_REQUEST; ++slice_id) {
                for (int channel = 0; channel < platform->channels; ++channel) {
                    if (event_append_command(
                            commands,
                            command_count,
                            IFC_OP_READ_SLICE,
                            tile_id / IFC_READ_SLICES_PER_REQUEST,
                            slice_id,
                            channel,
                            chip,
                            die,
                            read_plane) != 0) {
                        return -1;
                    }
                }
            }
        }
    }
    return 0;
}

static int event_stage_count(const IfcEventCommand *command) {
    return command->opcode == IFC_OP_READ_COMPUTE ? 4 : 2;
}

static const char *event_stage_name(const IfcEventCommand *command) {
    if (command->opcode == IFC_OP_READ_COMPUTE) {
        switch (command->stage_index) {
        case 0:
            return "SSDSIM_CA_TRANSFER";
        case 1:
            return "IFC_VECTOR_TRANSFER";
        case 2:
            return "SSDSIM_ARRAY_READ";
        case 3:
            return "IFC_COMPUTE";
        default:
            return "DONE";
        }
    }
    switch (command->stage_index) {
    case 0:
        return "SSDSIM_CA_TRANSFER";
    case 1:
        return "SSDSIM_DATA_TRANSFER";
    default:
        return "DONE";
    }
}

static const char *event_subrequest_state(const IfcEventCommand *command) {
    const char *stage = event_stage_name(command);
    if (strcmp(stage, "SSDSIM_CA_TRANSFER") == 0) {
        return "SR_R_C_A_TRANSFER";
    }
    if (strcmp(stage, "IFC_VECTOR_TRANSFER") == 0) {
        return "SR_IFC_VECTOR_TRANSFER";
    }
    if (strcmp(stage, "SSDSIM_ARRAY_READ") == 0) {
        return "SR_R_READ";
    }
    if (strcmp(stage, "IFC_COMPUTE") == 0) {
        return "SR_IFC_COMPUTE";
    }
    return "SR_R_DATA_TRANSFER";
}

static const char *event_channel_state(const IfcEventCommand *command) {
    const char *stage = event_stage_name(command);
    if (strcmp(stage, "SSDSIM_CA_TRANSFER") == 0) {
        return "CHANNEL_C_A_TRANSFER";
    }
    if (strcmp(stage, "IFC_VECTOR_TRANSFER") == 0 || strcmp(stage, "SSDSIM_DATA_TRANSFER") == 0) {
        return "CHANNEL_DATA_TRANSFER";
    }
    return "CHANNEL_IDLE";
}

static const char *event_chip_state(const IfcEventCommand *command) {
    const char *stage = event_stage_name(command);
    if (strcmp(stage, "SSDSIM_CA_TRANSFER") == 0) {
        return "CHIP_C_A_TRANSFER";
    }
    if (strcmp(stage, "IFC_VECTOR_TRANSFER") == 0 || strcmp(stage, "SSDSIM_DATA_TRANSFER") == 0) {
        return "CHIP_DATA_TRANSFER";
    }
    if (strcmp(stage, "SSDSIM_ARRAY_READ") == 0) {
        return "CHIP_READ_BUSY";
    }
    if (strcmp(stage, "IFC_COMPUTE") == 0) {
        return "CHIP_IFC_COMPUTE";
    }
    return "CHIP_IDLE";
}

static const char *event_plane_state(const IfcEventCommand *command) {
    const char *stage = event_stage_name(command);
    if (strcmp(stage, "SSDSIM_CA_TRANSFER") == 0) {
        return "PLANE_CMD_LATCH";
    }
    if (strcmp(stage, "IFC_VECTOR_TRANSFER") == 0) {
        return "PLANE_INPUT_LATCH";
    }
    if (strcmp(stage, "SSDSIM_ARRAY_READ") == 0) {
        return "PLANE_ARRAY_BUSY";
    }
    if (strcmp(stage, "IFC_COMPUTE") == 0) {
        return "PLANE_IFC_COMPUTE";
    }
    return "PLANE_DATA_REGISTER";
}

static long long event_stage_duration(const IfcEventCommand *command, const IfcSsdSimTiming *timing) {
    const char *stage = event_stage_name(command);
    if (strcmp(stage, "SSDSIM_CA_TRANSFER") == 0) {
        return timing->command_address_cycles;
    }
    if (strcmp(stage, "IFC_VECTOR_TRANSFER") == 0) {
        return timing->read_compute_vector_cycles;
    }
    if (strcmp(stage, "SSDSIM_ARRAY_READ") == 0) {
        return timing->array_read_cycles;
    }
    if (strcmp(stage, "IFC_COMPUTE") == 0) {
        return timing->ifc_compute_cycles;
    }
    return timing->read_slice_data_cycles;
}

static int event_resources_free(const IfcEventBackend *backend, const IfcEventCommand *command) {
    int channel = command->channel;
    int chip = command->chip;
    int die = command->die;
    int plane = command->plane;
    const char *stage = event_stage_name(command);

    if (strcmp(stage, "SSDSIM_CA_TRANSFER") == 0 ||
        strcmp(stage, "IFC_VECTOR_TRANSFER") == 0 ||
        strcmp(stage, "SSDSIM_DATA_TRANSFER") == 0) {
        return backend->channel_active[channel] < 0 &&
               backend->chip_active[channel][chip] < 0;
    }
    if (strcmp(stage, "SSDSIM_ARRAY_READ") == 0) {
        return backend->chip_active[channel][chip] < 0 &&
               backend->plane_active[channel][chip][die][plane] < 0;
    }
    return backend->chip_active[channel][chip] < 0 &&
           backend->plane_active[channel][chip][die][plane] < 0 &&
           backend->compute_active[channel][chip][die] < 0;
}

static void event_reserve_resources(IfcEventBackend *backend, const IfcEventCommand *command, int command_id) {
    int channel = command->channel;
    int chip = command->chip;
    int die = command->die;
    int plane = command->plane;
    const char *stage = event_stage_name(command);

    if (strcmp(stage, "SSDSIM_CA_TRANSFER") == 0 ||
        strcmp(stage, "IFC_VECTOR_TRANSFER") == 0 ||
        strcmp(stage, "SSDSIM_DATA_TRANSFER") == 0) {
        backend->channel_active[channel] = command_id;
        backend->chip_active[channel][chip] = command_id;
    } else if (strcmp(stage, "SSDSIM_ARRAY_READ") == 0) {
        backend->chip_active[channel][chip] = command_id;
        backend->plane_active[channel][chip][die][plane] = command_id;
    } else {
        backend->chip_active[channel][chip] = command_id;
        backend->plane_active[channel][chip][die][plane] = command_id;
        backend->compute_active[channel][chip][die] = command_id;
    }
}

static void event_release_resources(IfcEventBackend *backend, const IfcEventCommand *command, int command_id) {
    int channel = command->channel;
    int chip = command->chip;
    int die = command->die;
    int plane = command->plane;
    if (backend->channel_active[channel] == command_id) {
        backend->channel_active[channel] = -1;
    }
    if (backend->chip_active[channel][chip] == command_id) {
        backend->chip_active[channel][chip] = -1;
    }
    if (backend->plane_active[channel][chip][die][plane] == command_id) {
        backend->plane_active[channel][chip][die][plane] = -1;
    }
    if (backend->compute_active[channel][chip][die] == command_id) {
        backend->compute_active[channel][chip][die] = -1;
    }
}

static int write_event_row(
    FILE *file,
    IfcEventBackend *backend,
    long long event_cycle,
    const char *event_type,
    int command_id,
    const IfcEventCommand *command,
    int active_commands) {
    if (fprintf(
            file,
            "%d,%lld,%s,%d,%s,%d,%d,%s,%s,%s,%s,%s,%d,%d,%d,%d,%lld,%lld,%lld,%d\n",
            backend->events,
            event_cycle,
            event_type,
            command_id,
            ifc_opcode_name(command->opcode),
            command->logical_id,
            command->slice_id,
            event_stage_name(command),
            event_subrequest_state(command),
            event_channel_state(command),
            event_chip_state(command),
            event_plane_state(command),
            command->channel,
            command->chip,
            command->die,
            command->plane,
            command->stage_start_cycle,
            command->stage_end_cycle,
            command->stage_duration_cycles,
            active_commands) < 0) {
        return -1;
    }
    ++backend->events;
    if (strcmp(event_type, "ISSUE") == 0) {
        ++backend->issue_events;
    } else {
        ++backend->complete_events;
    }
    backend->last_event_cycle = event_cycle;
    if (active_commands > backend->max_active_commands) {
        backend->max_active_commands = active_commands;
    }
    return 0;
}

static int write_event_stats(
    const char *path,
    const IfcPlatformProfile *platform,
    const IfcSsdSimTiming *timing,
    const IfcEventBackend *backend,
    int command_count) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    fprintf(file, "metric,value\n");
    fprintf(file, "cycle_ns,%.6f\n", timing->cycle_ns);
    fprintf(file, "commands,%d\n", command_count);
    fprintf(file, "completed_commands,%d\n", backend->completed_commands);
    fprintf(file, "events,%d\n", backend->events);
    fprintf(file, "issue_events,%d\n", backend->issue_events);
    fprintf(file, "complete_events,%d\n", backend->complete_events);
    fprintf(file, "max_active_commands,%d\n", backend->max_active_commands);
    fprintf(file, "last_event_cycle,%lld\n", backend->last_event_cycle);
    fprintf(file, "last_event_ns,%.6f\n", (double)backend->last_event_cycle * timing->cycle_ns);
    fprintf(file, "command_address_cycles,%lld\n", timing->command_address_cycles);
    fprintf(file, "read_compute_vector_cycles,%lld\n", timing->read_compute_vector_cycles);
    fprintf(file, "read_slice_data_cycles,%lld\n", timing->read_slice_data_cycles);
    fprintf(file, "array_read_cycles,%lld\n", timing->array_read_cycles);
    fprintf(file, "ifc_compute_cycles,%lld\n", timing->ifc_compute_cycles);
    fprintf(file, "channels,%d\n", platform->channels);
    fprintf(file, "chips_per_channel,%d\n", platform->chips_per_channel);
    fprintf(file, "dies_per_chip,%d\n", platform->dies_per_chip);
    fprintf(file, "planes_per_die,%d\n", platform->planes_per_die);
    return fclose(file);
}

int ifc_write_ssdsim_ifc_event_trace_for_platform(
    const char *trace_path,
    const char *stats_path,
    const IfcPlatformProfile *platform) {
    FILE *file;
    IfcEventCommand commands[IFC_SSDSIM_MAX_COMMANDS];
    IfcEventBackend backend;
    IfcSsdSimTiming timing;
    int command_count = 0;
    int active_commands = 0;
    long long current_cycle = 0;
    long long guard = 0;

    if (!ssdsim_platform_supported(platform)) {
        return -1;
    }
    if (event_build_commands(commands, &command_count, platform) != 0) {
        return -1;
    }

    file = fopen(trace_path, "w");
    if (file == NULL) {
        return -1;
    }
    event_backend_init(&backend);
    timing = ssdsim_derive_timing(platform);
    fprintf(file,
            "event_id,event_cycle,event_type,command_id,opcode,logical_id,slice_id,stage,subrequest_state,"
            "channel_state,chip_state,plane_state,channel,chip,die,plane,stage_start_cycle,stage_end_cycle,"
            "duration_cycles,active_commands\n");

    while (backend.completed_commands < command_count && guard < 10000000LL) {
        int completed_this_cycle = 0;
        int issued_this_cycle = 0;
        long long next_cycle = IFC_SSDSIM_INF_CYCLE;

        for (int i = 0; i < command_count; ++i) {
            if (commands[i].status == IFC_EVENT_ACTIVE && commands[i].stage_end_cycle == current_cycle) {
                if (write_event_row(file, &backend, current_cycle, "COMPLETE", i, &commands[i], active_commands) != 0) {
                    fclose(file);
                    return -1;
                }
                event_release_resources(&backend, &commands[i], i);
                --active_commands;
                ++completed_this_cycle;
                ++commands[i].stage_index;
                if (commands[i].stage_index >= event_stage_count(&commands[i])) {
                    commands[i].status = IFC_EVENT_DONE;
                    ++backend.completed_commands;
                } else {
                    commands[i].status = IFC_EVENT_WAITING;
                }
            }
        }

        for (int i = 0; i < command_count; ++i) {
            if (commands[i].status == IFC_EVENT_WAITING &&
                commands[i].arrival_cycle <= current_cycle &&
                event_resources_free(&backend, &commands[i])) {
                commands[i].stage_duration_cycles = event_stage_duration(&commands[i], &timing);
                commands[i].stage_start_cycle = current_cycle;
                commands[i].stage_end_cycle = current_cycle + commands[i].stage_duration_cycles;
                commands[i].status = IFC_EVENT_ACTIVE;
                event_reserve_resources(&backend, &commands[i], i);
                ++active_commands;
                ++issued_this_cycle;
                if (write_event_row(file, &backend, current_cycle, "ISSUE", i, &commands[i], active_commands) != 0) {
                    fclose(file);
                    return -1;
                }
            }
        }

        if (backend.completed_commands >= command_count) {
            break;
        }
        for (int i = 0; i < command_count; ++i) {
            if (commands[i].status == IFC_EVENT_ACTIVE && commands[i].stage_end_cycle < next_cycle) {
                next_cycle = commands[i].stage_end_cycle;
            } else if (commands[i].status == IFC_EVENT_WAITING &&
                       commands[i].arrival_cycle > current_cycle &&
                       commands[i].arrival_cycle < next_cycle) {
                next_cycle = commands[i].arrival_cycle;
            }
        }
        if (next_cycle == IFC_SSDSIM_INF_CYCLE || next_cycle < current_cycle) {
            fclose(file);
            return -1;
        }
        if (next_cycle == current_cycle && completed_this_cycle == 0 && issued_this_cycle == 0) {
            fclose(file);
            return -1;
        }
        current_cycle = next_cycle;
        ++guard;
    }

    if (backend.completed_commands != command_count) {
        fclose(file);
        return -1;
    }
    if (fclose(file) != 0) {
        return -1;
    }
    return write_event_stats(stats_path, platform, &timing, &backend, command_count);
}
