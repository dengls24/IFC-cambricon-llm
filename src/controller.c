#include "ifc_cambricon_llm.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    double channel_busy_until_ns[64];
    double plane_busy_until_ns[64][128][2][2];
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

static double max2(double lhs, double rhs) {
    return lhs > rhs ? lhs : rhs;
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
