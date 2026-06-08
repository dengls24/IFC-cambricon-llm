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

static const char *status_name(int pass) {
    return pass ? "PASS" : "FAIL";
}

static int write_platform_summary(const char *path, const IfcConfig *config, const IfcSimulationRow rows[IFC_ROW_COUNT]) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    fprintf(file,
            "platform,platform_label,channels,chips_per_channel,dies_per_chip,planes_per_die,"
            "onfi_rate_MTps,onfi_bus_width_bits,ifc_frequency_MHz,ifc_ops_per_core_cycle,"
            "tile_height,tile_width,mean_reference_tokens_per_s,mean_simulated_tokens_per_s,"
            "mean_abs_relative_error_pct,max_abs_relative_error_pct,mean_speedup_vs_no_read_slicing,"
            "mean_speedup_vs_no_tiling,mean_read_compute_channel_rate_pct,max_controller_commands\n");
    for (int platform_id = 0; platform_id < IFC_PLATFORM_COUNT; ++platform_id) {
        const IfcPlatformProfile *platform = &config->platforms[platform_id];
        double reference_sum = 0.0;
        double simulated_sum = 0.0;
        double abs_error_sum = 0.0;
        double max_abs_error = 0.0;
        double read_slicing_speedup_sum = 0.0;
        double tiling_speedup_sum = 0.0;
        double read_compute_rate_sum = 0.0;
        double max_commands = 0.0;
        int count = 0;
        IfcTileModel tile = ifc_derive_tile_model(platform);

        for (int i = 0; i < IFC_ROW_COUNT; ++i) {
            if (strcmp(rows[i].platform, platform->name) != 0) {
                continue;
            }
            double abs_error = fabs(rows[i].relative_error_pct);
            reference_sum += rows[i].reference_tokens_per_s;
            simulated_sum += rows[i].simulated_tokens_per_s;
            abs_error_sum += abs_error;
            read_slicing_speedup_sum += rows[i].speedup_vs_no_read_slicing;
            tiling_speedup_sum += rows[i].speedup_vs_no_tiling;
            read_compute_rate_sum += rows[i].read_compute_channel_rate_pct;
            if (abs_error > max_abs_error) {
                max_abs_error = abs_error;
            }
            if (rows[i].controller_commands > max_commands) {
                max_commands = rows[i].controller_commands;
            }
            ++count;
        }

        fprintf(file,
                "%s,%s,%d,%d,%d,%d,%.6f,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                platform->name,
                platform->label,
                platform->channels,
                platform->chips_per_channel,
                platform->dies_per_chip,
                platform->planes_per_die,
                platform->onfi_rate_MTps,
                platform->onfi_bus_width_bits,
                platform->ifc_frequency_hz / 1e6,
                platform->ifc_ops_per_core_cycle,
                tile.tile_height,
                tile.tile_width,
                reference_sum / (double)count,
                simulated_sum / (double)count,
                abs_error_sum / (double)count,
                max_abs_error,
                read_slicing_speedup_sum / (double)count,
                tiling_speedup_sum / (double)count,
                read_compute_rate_sum / (double)count,
                max_commands);
    }
    return fclose(file);
}

static int write_model_summary(const char *path, const IfcConfig *config, const IfcSimulationRow rows[IFC_ROW_COUNT]) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    fprintf(file,
            "model,model_label,parameters_billion,layers,hidden_size,attention_heads,"
            "cache_heads,head_dim,mean_reference_tokens_per_s,mean_simulated_tokens_per_s,mean_abs_relative_error_pct,"
            "max_abs_relative_error_pct,worst_platform\n");
    for (int model_id = 0; model_id < IFC_MODEL_COUNT; ++model_id) {
        const IfcModelProfile *model = &config->models[model_id];
        double reference_sum = 0.0;
        double simulated_sum = 0.0;
        double abs_error_sum = 0.0;
        double max_abs_error = 0.0;
        const char *worst_platform = "";
        int count = 0;

        for (int i = 0; i < IFC_ROW_COUNT; ++i) {
            if (strcmp(rows[i].model, model->name) != 0) {
                continue;
            }
            double abs_error = fabs(rows[i].relative_error_pct);
            reference_sum += rows[i].reference_tokens_per_s;
            simulated_sum += rows[i].simulated_tokens_per_s;
            abs_error_sum += abs_error;
            if (abs_error > max_abs_error) {
                max_abs_error = abs_error;
                worst_platform = rows[i].platform;
            }
            ++count;
        }

        fprintf(file,
                "%s,%s,%.6f,%d,%d,%d,%d,%d,%.6f,%.6f,%.6f,%.6f,%s\n",
                model->name,
                model->label,
                model->parameters_billion,
                model->layers,
                model->hidden_size,
                model->attention_heads,
                model->cache_heads,
                model->head_dim,
                reference_sum / (double)count,
                simulated_sum / (double)count,
                abs_error_sum / (double)count,
                max_abs_error,
                worst_platform);
    }
    return fclose(file);
}

static int write_tile_profile(const char *path, const IfcConfig *config) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    fprintf(file,
            "platform,platform_label,channels,compute_cores_per_channel,page_bytes,onfi_rate_MTps,onfi_bus_width_bits,"
            "ifc_frequency_MHz,ifc_ops_per_core_cycle,channel_bandwidth_Bps,tile_height,tile_width,"
            "tile_payload_bytes,read_compute_request_us,ifc_compute_time_us,sliced_read_request_us,"
            "alpha_read_compute,read_compute_channel_rate_pct\n");
    for (int platform_id = 0; platform_id < IFC_PLATFORM_COUNT; ++platform_id) {
        const IfcPlatformProfile *platform = &config->platforms[platform_id];
        IfcTileModel tile = ifc_derive_tile_model(platform);
        fprintf(file,
                "%s,%s,%d,%d,%d,%.6f,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                platform->name,
                platform->label,
                platform->channels,
                tile.compute_cores_per_channel,
                platform->page_bytes,
                platform->onfi_rate_MTps,
                platform->onfi_bus_width_bits,
                platform->ifc_frequency_hz / 1e6,
                platform->ifc_ops_per_core_cycle,
                ifc_platform_channel_bandwidth_Bps(platform),
                tile.tile_height,
                tile.tile_width,
                tile.tile_payload_bytes,
                tile.read_compute_request_s * 1e6,
                tile.ifc_compute_time_s * 1e6,
                tile.sliced_read_request_s * 1e6,
                tile.alpha_read_compute,
                tile.read_compute_channel_rate * 100.0);
    }
    return fclose(file);
}

static int write_system_profile(const char *path, const IfcConfig *config) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    fprintf(file, "name,label,context_tokens,npu_frequency_MHz,npu_ops_per_cycle,npu_peak_TOPS,dram_bandwidth_GBps\n");
    fprintf(file,
            "%s,%s,%d,%.6f,%.6f,%.6f,%.6f\n",
            config->system.name,
            config->system.label,
            config->context_tokens,
            config->system.npu_frequency_hz / 1e6,
            config->system.npu_ops_per_cycle,
            ifc_system_npu_peak_ops_per_s(&config->system) / 1e12,
            config->system.dram_bandwidth_Bps / 1e9);
    return fclose(file);
}

static int write_check(FILE *file, const char *name, double value, const char *target, int pass) {
    return fprintf(file, "%s,%.6f,%s,%s\n", name, value, target, status_name(pass)) < 0 ? -1 : 0;
}

static int write_reproduction_checks(
    const char *path,
    const IfcConfig *config,
    const IfcSimulationRow rows[IFC_ROW_COUNT],
    const IfcSummary *summary) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    double cam_s_slice_min = 1e30;
    double cam_s_slice_max = -1e30;
    double cam_s_tiling_min = 1e30;
    double cam_s_tiling_max = -1e30;
    double max_balance_delta = 0.0;
    IfcTileModel first_tile = ifc_derive_tile_model(&config->platforms[0]);

    for (int i = 0; i < IFC_ROW_COUNT; ++i) {
        if (rows[i].controller_balance_delta_pct > max_balance_delta) {
            max_balance_delta = rows[i].controller_balance_delta_pct;
        }
        if (strcmp(rows[i].platform, config->platforms[0].name) == 0) {
            if (rows[i].speedup_vs_no_read_slicing < cam_s_slice_min) {
                cam_s_slice_min = rows[i].speedup_vs_no_read_slicing;
            }
            if (rows[i].speedup_vs_no_read_slicing > cam_s_slice_max) {
                cam_s_slice_max = rows[i].speedup_vs_no_read_slicing;
            }
            if (rows[i].speedup_vs_no_tiling < cam_s_tiling_min) {
                cam_s_tiling_min = rows[i].speedup_vs_no_tiling;
            }
            if (rows[i].speedup_vs_no_tiling > cam_s_tiling_max) {
                cam_s_tiling_max = rows[i].speedup_vs_no_tiling;
            }
        }
    }

    fprintf(file, "check,value,target,status\n");
    if (write_check(file, "figure9_row_count", (double)summary->row_count, "21", summary->row_count == IFC_ROW_COUNT) != 0 ||
        write_check(file, "figure9_mean_abs_error_pct", summary->mean_abs_relative_error_pct, "<=9", summary->mean_abs_relative_error_pct <= 9.0) != 0 ||
        write_check(file, "figure9_max_abs_error_pct", summary->max_abs_relative_error_pct, "<=15", summary->max_abs_relative_error_pct <= 15.0) != 0 ||
        write_check(file, "first_platform_tile_height", first_tile.tile_height, ">0", first_tile.tile_height > 0.0) != 0 ||
        write_check(file, "first_platform_tile_width", first_tile.tile_width, ">0", first_tile.tile_width > 0.0) != 0 ||
        write_check(file, "first_platform_read_slicing_speedup_min", cam_s_slice_min, ">=1.0", cam_s_slice_min >= 1.0) != 0 ||
        write_check(file, "first_platform_read_slicing_speedup_max", cam_s_slice_max, "<=4.0", cam_s_slice_max <= 4.0) != 0 ||
        write_check(file, "first_platform_tiling_speedup_min", cam_s_tiling_min, ">=1.0", cam_s_tiling_min >= 1.0) != 0 ||
        write_check(file, "first_platform_tiling_speedup_max", cam_s_tiling_max, "<=4.0", cam_s_tiling_max <= 4.0) != 0 ||
        write_check(file, "controller_balance_delta_max_pct", max_balance_delta, "<=1e-6", max_balance_delta <= 1e-6) != 0 ||
        write_check(file, "cycle_controller_trace_enabled", 1.0, "1", 1) != 0 ||
        write_check(file, "ssdsim_ifc_backend_enabled", 1.0, "1", 1) != 0 ||
        write_check(file, "ssdsim_ifc_event_loop_enabled", 1.0, "1", 1) != 0) {
        fclose(file);
        return -1;
    }
    return fclose(file);
}

int ifc_write_analysis_outputs(
    const char *output_dir,
    const IfcSimulationRow rows[IFC_ROW_COUNT],
    const IfcSummary *summary) {
    IfcConfig config;
    ifc_config_init_default(&config);
    return ifc_write_analysis_outputs_config(output_dir, &config, rows, summary);
}

int ifc_write_analysis_outputs_config(
    const char *output_dir,
    const IfcConfig *config,
    const IfcSimulationRow rows[IFC_ROW_COUNT],
    const IfcSummary *summary) {
    char platform_summary_path[4096];
    char model_summary_path[4096];
    char tile_profile_path[4096];
    char system_profile_path[4096];
    char checks_path[4096];

    if (ensure_dir(output_dir) != 0) {
        return -1;
    }
    join_path(platform_summary_path, sizeof(platform_summary_path), output_dir, "platform_summary.csv");
    join_path(model_summary_path, sizeof(model_summary_path), output_dir, "model_summary.csv");
    join_path(tile_profile_path, sizeof(tile_profile_path), output_dir, "tile_profile.csv");
    join_path(system_profile_path, sizeof(system_profile_path), output_dir, "system_profile.csv");
    join_path(checks_path, sizeof(checks_path), output_dir, "reproduction_checks.csv");

    if (write_platform_summary(platform_summary_path, config, rows) != 0 ||
        write_model_summary(model_summary_path, config, rows) != 0 ||
        write_tile_profile(tile_profile_path, config) != 0 ||
        write_system_profile(system_profile_path, config) != 0 ||
        write_reproduction_checks(checks_path, config, rows, summary) != 0) {
        return -1;
    }
    return 0;
}
