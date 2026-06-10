#include "ifc_cambricon_llm.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static int ensure_dir(const char *path) {
    if (mkdir(path, 0775) == 0) {
        return 0;
    }
    if (errno == EEXIST) {
        return 0;
    }
    return -1;
}

static void join_path(char *buffer, size_t buffer_size, const char *dir, const char *name) {
    size_t len = strlen(dir);
    if (len > 0 && dir[len - 1] == '/') {
        snprintf(buffer, buffer_size, "%s%s", dir, name);
    } else {
        snprintf(buffer, buffer_size, "%s/%s", dir, name);
    }
}

static int write_manifest(const char *path) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    fprintf(file, "artifact,format,release_role\n");
    fprintf(file, "figure9_plot_source.csv,csv,local helper source table\n");
    fprintf(file, "ablation_plot_source.csv,csv,local helper source table\n");
    fprintf(file, "controller_timeline_source.csv,csv,local helper source table\n");
    fprintf(file, "docs/figures/*.png,png,release figures\n");
    fprintf(file, "docs/figures/*.pdf,pdf,release figures\n");
    return fclose(file);
}

static int write_figure9_plot_source(
    const char *path,
    const IfcSimulationRow rows[IFC_ROW_COUNT],
    const IfcSummary *summary) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    fprintf(file,
            "model,platform,reference_tokens_per_s,simulated_tokens_per_s,relative_error_pct,"
            "tpot_ms,weight_stage_ms,attention_cache_ms,attention_compute_ms,mean_abs_error_pct,max_abs_error_pct\n");
    for (int i = 0; i < IFC_ROW_COUNT; ++i) {
        fprintf(file,
                "%s,%s,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                rows[i].model,
                rows[i].platform,
                rows[i].reference_tokens_per_s,
                rows[i].simulated_tokens_per_s,
                rows[i].relative_error_pct,
                rows[i].tpot_ms,
                rows[i].weight_stage_ms,
                rows[i].attention_cache_ms,
                rows[i].attention_compute_ms,
                summary->mean_abs_relative_error_pct,
                summary->max_abs_relative_error_pct);
    }
    return fclose(file);
}

static int write_ablation_plot_source(
    const char *path,
    const IfcSimulationRow rows[IFC_ROW_COUNT]) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    fprintf(file,
            "model,platform,full_tokens_per_s,no_read_slicing_tokens_per_s,no_tiling_tokens_per_s,"
            "speedup_vs_no_read_slicing,speedup_vs_no_tiling\n");
    for (int i = 0; i < IFC_ROW_COUNT; ++i) {
        fprintf(file,
                "%s,%s,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                rows[i].model,
                rows[i].platform,
                rows[i].simulated_tokens_per_s,
                rows[i].no_read_slicing_tokens_per_s,
                rows[i].no_tiling_tokens_per_s,
                rows[i].speedup_vs_no_read_slicing,
                rows[i].speedup_vs_no_tiling);
    }
    return fclose(file);
}

static int write_platform_plot_source(
    const char *path,
    const IfcConfig *config,
    const IfcSimulationRow rows[IFC_ROW_COUNT]) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    fprintf(file, "platform,mean_abs_relative_error_pct,max_abs_relative_error_pct,mean_tokens_per_s\n");
    for (int platform_id = 0; platform_id < IFC_PLATFORM_COUNT; ++platform_id) {
        double abs_error_sum = 0.0;
        double max_abs_error = 0.0;
        double tps_sum = 0.0;
        int count = 0;
        for (int i = 0; i < IFC_ROW_COUNT; ++i) {
            double abs_error;
            if (strcmp(rows[i].platform, config->platforms[platform_id].name) != 0) {
                continue;
            }
            abs_error = fabs(rows[i].relative_error_pct);
            abs_error_sum += abs_error;
            if (abs_error > max_abs_error) {
                max_abs_error = abs_error;
            }
            tps_sum += rows[i].simulated_tokens_per_s;
            ++count;
        }
        fprintf(file,
                "%s,%.6f,%.6f,%.6f\n",
                config->platforms[platform_id].name,
                abs_error_sum / (double)count,
                max_abs_error,
                tps_sum / (double)count);
    }
    return fclose(file);
}

static int write_controller_timeline_source(const char *path, const IfcConfig *config) {
    const IfcPlatformProfile *platform = &config->platforms[0];
    IfcTileModel tile = ifc_derive_tile_model(platform);
    double channel_bandwidth_Bps = ifc_platform_channel_bandwidth_Bps(platform);
    double channel_transfer_us = tile.tile_width / (double)platform->channels / channel_bandwidth_Bps * 1e6;
    double read_compute_us = platform->array_read_us + channel_transfer_us;
    double slice_us =
        ((double)platform->page_bytes / (double)IFC_READ_SLICES_PER_REQUEST) / channel_bandwidth_Bps * 1e6;
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    fprintf(file, "channel,tile_id,stage,start_us,duration_us\n");
    for (int tile_id = 0; tile_id < IFC_SAMPLE_TILES; ++tile_id) {
        double rc_start = (double)tile_id * read_compute_us;
        for (int channel = 0; channel < platform->channels; ++channel) {
            fprintf(file, "%d,%d,READ_COMPUTE,%.6f,%.6f\n", channel, tile_id, rc_start, read_compute_us);
            if ((tile_id + 1) % IFC_READ_SLICES_PER_REQUEST == 0) {
                double slice_start = rc_start + channel_transfer_us;
                for (int slice = 0; slice < IFC_READ_SLICES_PER_REQUEST; ++slice) {
                    fprintf(file,
                            "%d,%d,READ_SLICE_%d,%.6f,%.6f\n",
                            channel,
                            tile_id,
                            slice,
                            slice_start + (double)slice * slice_us,
                            slice_us);
                }
            }
        }
    }
    return fclose(file);
}

int ifc_write_plots(const char *output_dir, const IfcSimulationRow rows[IFC_ROW_COUNT], const IfcSummary *summary) {
    IfcConfig config;
    ifc_config_init_default(&config);
    return ifc_write_plots_config(output_dir, &config, rows, summary);
}

int ifc_write_plots_config(
    const char *output_dir,
    const IfcConfig *config,
    const IfcSimulationRow rows[IFC_ROW_COUNT],
    const IfcSummary *summary) {
    char figures_dir[4096];
    char manifest_path[4096];
    char figure9_path[4096];
    char ablation_path[4096];
    char platform_path[4096];
    char timeline_path[4096];

    join_path(figures_dir, sizeof(figures_dir), output_dir, "figures");
    if (ensure_dir(figures_dir) != 0) {
        return -1;
    }
    join_path(manifest_path, sizeof(manifest_path), figures_dir, "plot_manifest.csv");
    join_path(figure9_path, sizeof(figure9_path), figures_dir, "figure9_plot_source.csv");
    join_path(ablation_path, sizeof(ablation_path), figures_dir, "ablation_plot_source.csv");
    join_path(platform_path, sizeof(platform_path), figures_dir, "platform_plot_source.csv");
    join_path(timeline_path, sizeof(timeline_path), figures_dir, "controller_timeline_source.csv");

    if (write_manifest(manifest_path) != 0) {
        return -1;
    }
    if (write_figure9_plot_source(figure9_path, rows, summary) != 0) {
        return -1;
    }
    if (write_ablation_plot_source(ablation_path, rows) != 0) {
        return -1;
    }
    if (write_platform_plot_source(platform_path, config, rows) != 0) {
        return -1;
    }
    if (write_controller_timeline_source(timeline_path, config) != 0) {
        return -1;
    }
    return 0;
}
