#include "ifc_cambricon_llm.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static double attention_cache_bytes(const IfcModelProfile *model, int context_tokens) {
    return (double)model->layers * 2.0 * (double)model->cache_heads * (double)model->head_dim * (double)context_tokens;
}

static double attention_ops(const IfcModelProfile *model, int context_tokens) {
    return 2.0 * (double)model->layers * (double)model->attention_heads * (double)model->head_dim * (double)context_tokens;
}

static double effective_efficiency(const IfcModelProfile *model, const IfcPlatformProfile *platform) {
    double scale = model->parameters_billion / 70.0;
    return platform->pipeline_efficiency /
           (1.0 + platform->footprint_penalty * pow(scale, platform->footprint_penalty_power));
}

static void join_path(char *buffer, size_t buffer_size, const char *dir, const char *name) {
    size_t len = strlen(dir);
    if (len > 0 && dir[len - 1] == '/') {
        snprintf(buffer, buffer_size, "%s%s", dir, name);
    } else {
        snprintf(buffer, buffer_size, "%s/%s", dir, name);
    }
}

static int ensure_dir(const char *path) {
    if (mkdir(path, 0775) == 0) {
        return 0;
    }
    if (errno == EEXIST) {
        return 0;
    }
    return -1;
}

static double max2(double lhs, double rhs) {
    return lhs > rhs ? lhs : rhs;
}

IfcTileModel ifc_derive_tile_model(const IfcPlatformProfile *platform) {
    IfcTileModel tile;
    double channels = (double)platform->channels;
    double page_bytes = (double)platform->page_bytes;
    double channel_bw = ifc_platform_channel_bandwidth_Bps(platform);
    double array_read_s = platform->array_read_us * 1e-6;
    double ifc_total_cores =
        channels * (double)platform->chips_per_channel * (double)platform->dies_per_chip *
        (double)platform->compute_cores_per_die;
    double ifc_peak_ops = ifc_total_cores * platform->ifc_frequency_hz * platform->ifc_ops_per_core_cycle;
    double vector_transfer_s;
    double tile_ops;

    tile.compute_cores_per_channel =
        platform->chips_per_channel * platform->dies_per_chip * platform->compute_cores_per_die;
    tile.tile_height = sqrt((double)tile.compute_cores_per_channel * page_bytes);
    tile.tile_width = channels * tile.tile_height;
    tile.tile_payload_bytes = channels * (double)tile.compute_cores_per_channel * page_bytes;
    vector_transfer_s = tile.tile_width / (channels * channel_bw);
    tile_ops = tile.tile_height * tile.tile_width;
    tile.ifc_compute_time_s = ifc_peak_ops > 0.0 ? tile_ops / ifc_peak_ops : 0.0;
    tile.read_compute_request_s =
        array_read_s + (vector_transfer_s > tile.ifc_compute_time_s ? vector_transfer_s : tile.ifc_compute_time_s);
    tile.read_compute_channel_rate =
        (tile.tile_height + tile.tile_width / channels) / (array_read_s * channel_bw);
    tile.sliced_read_request_s =
        page_bytes / ((1.0 - tile.read_compute_channel_rate) * channel_bw);
    tile.alpha_read_compute =
        tile.sliced_read_request_s / (tile.sliced_read_request_s + tile.read_compute_request_s);
    return tile;
}

IfcSimulationRow ifc_simulate_one(
    const IfcModelProfile *model,
    const IfcPlatformProfile *platform,
    double reference_tokens_per_s,
    int context_tokens) {
    return ifc_simulate_one_with_system(model, platform, &IFC_DEFAULT_SYSTEM, reference_tokens_per_s, context_tokens);
}

IfcSimulationRow ifc_simulate_one_with_system(
    const IfcModelProfile *model,
    const IfcPlatformProfile *platform,
    const IfcSystemProfile *system,
    double reference_tokens_per_s,
    int context_tokens) {
    IfcTileModel tile = ifc_derive_tile_model(platform);
    double weight_bytes = model->parameters_billion * 1e9;
    double request_units = weight_bytes / tile.tile_payload_bytes;
    double read_compute_requests = request_units * tile.alpha_read_compute;
    double npu_read_requests = request_units * (1.0 - tile.alpha_read_compute);
    double npu_read_slices = npu_read_requests * (double)IFC_READ_SLICES_PER_REQUEST;
    double read_compute_s = read_compute_requests * tile.read_compute_request_s;
    double sliced_read_s = npu_read_requests * tile.sliced_read_request_s;
    double overlapped_weight_s = read_compute_s > sliced_read_s ? read_compute_s : sliced_read_s;
    double efficiency = effective_efficiency(model, platform);
    double weight_stage_s = overlapped_weight_s / efficiency;
    double att_cache_bytes = attention_cache_bytes(model, context_tokens);
    double att_ops = attention_ops(model, context_tokens);
    double attention_cache_s = att_cache_bytes / system->dram_bandwidth_Bps;
    double attention_compute_s = att_ops / ifc_system_npu_peak_ops_per_s(system);
    double tpot_s = weight_stage_s + attention_cache_s + attention_compute_s;
    double simulated_speed = 1.0 / tpot_s;

    double no_slice_weight_s =
        (read_compute_s + sliced_read_s) * platform->unsliced_blocking_factor / efficiency;
    double no_slice_tpot_s = no_slice_weight_s + attention_cache_s + attention_compute_s;
    double no_tiling_tpot_s =
        weight_stage_s * platform->no_tiling_slowdown + attention_cache_s + attention_compute_s;

    IfcSimulationRow row;
    row.model = model->name;
    row.model_label = model->label;
    row.platform = platform->name;
    row.platform_label = platform->label;
    row.context_tokens = context_tokens;
    row.reference_tokens_per_s = reference_tokens_per_s;
    row.simulated_tokens_per_s = simulated_speed;
    row.relative_error_pct = (simulated_speed - reference_tokens_per_s) / reference_tokens_per_s * 100.0;
    row.weight_bytes = weight_bytes;
    row.attention_cache_bytes = att_cache_bytes;
    row.attention_ops = att_ops;
    row.tpot_ms = tpot_s * 1e3;
    row.weight_stage_ms = weight_stage_s * 1e3;
    row.attention_cache_ms = attention_cache_s * 1e3;
    row.attention_compute_ms = attention_compute_s * 1e3;
    row.tile_height = tile.tile_height;
    row.tile_width = tile.tile_width;
    row.alpha_read_compute = tile.alpha_read_compute;
    row.effective_pipeline_efficiency = efficiency;
    row.read_compute_requests = read_compute_requests;
    row.npu_read_requests = npu_read_requests;
    row.npu_read_slices = npu_read_slices;
    row.controller_commands = read_compute_requests + npu_read_slices;
    row.read_compute_channel_rate_pct = tile.read_compute_channel_rate * 100.0;
    row.ifc_read_compute_path_ms = read_compute_s / efficiency * 1e3;
    row.npu_weight_read_path_ms = sliced_read_s / efficiency * 1e3;
    row.controller_weight_stage_ms = weight_stage_s * 1e3;
    row.controller_balance_delta_pct =
        fabs(row.ifc_read_compute_path_ms - row.npu_weight_read_path_ms) /
        max2(row.controller_weight_stage_ms, 1e-12) * 100.0;
    row.no_read_slicing_tokens_per_s = 1.0 / no_slice_tpot_s;
    row.no_tiling_tokens_per_s = 1.0 / no_tiling_tpot_s;
    row.speedup_vs_no_read_slicing = no_slice_tpot_s / tpot_s;
    row.speedup_vs_no_tiling = no_tiling_tpot_s / tpot_s;
    return row;
}

IfcSummary ifc_simulate_reproduction(IfcSimulationRow rows[IFC_ROW_COUNT], int context_tokens) {
    IfcConfig config;
    ifc_config_init_default(&config);
    config.context_tokens = context_tokens;
    return ifc_simulate_config(&config, rows);
}

IfcSummary ifc_simulate_config(const IfcConfig *config, IfcSimulationRow rows[IFC_ROW_COUNT]) {
    IfcSummary summary;
    double abs_error_sum = 0.0;
    double error_sum = 0.0;
    double max_abs_error = -1.0;
    int row_id = 0;

    summary.row_count = IFC_ROW_COUNT;
    summary.worst_case_model = "";
    summary.worst_case_platform = "";

    for (int model_id = 0; model_id < IFC_MODEL_COUNT; ++model_id) {
        for (int platform_id = 0; platform_id < IFC_PLATFORM_COUNT; ++platform_id) {
            rows[row_id] = ifc_simulate_one_with_system(
                &config->models[model_id],
                &config->platforms[platform_id],
                &config->system,
                config->reference_tokens_per_s[model_id][platform_id],
                config->context_tokens);
            double abs_error = fabs(rows[row_id].relative_error_pct);
            abs_error_sum += abs_error;
            error_sum += rows[row_id].relative_error_pct;
            if (abs_error > max_abs_error) {
                max_abs_error = abs_error;
                summary.worst_case_model = rows[row_id].model;
                summary.worst_case_platform = rows[row_id].platform;
            }
            ++row_id;
        }
    }

    summary.mean_abs_relative_error_pct = abs_error_sum / (double)IFC_ROW_COUNT;
    summary.max_abs_relative_error_pct = max_abs_error;
    summary.mean_relative_error_pct = error_sum / (double)IFC_ROW_COUNT;
    return summary;
}

static int write_csv(const char *path, const IfcSimulationRow rows[IFC_ROW_COUNT]) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    fprintf(file,
            "model,model_label,platform,platform_label,context_tokens,reference_tokens_per_s,"
            "simulated_tokens_per_s,relative_error_pct,weight_bytes,attention_cache_bytes,attention_ops,"
            "tpot_ms,weight_stage_ms,attention_cache_ms,attention_compute_ms,tile_height,tile_width,"
            "alpha_read_compute,effective_pipeline_efficiency,"
            "read_compute_requests,npu_read_requests,npu_read_slices,controller_commands,"
            "read_compute_channel_rate_pct,ifc_read_compute_path_ms,npu_weight_read_path_ms,"
            "controller_weight_stage_ms,controller_balance_delta_pct,no_read_slicing_tokens_per_s,"
            "no_tiling_tokens_per_s,speedup_vs_no_read_slicing,speedup_vs_no_tiling\n");
    for (int i = 0; i < IFC_ROW_COUNT; ++i) {
        const IfcSimulationRow *r = &rows[i];
        fprintf(file,
                "%s,%s,%s,%s,%d,%.6f,%.6f,%.3f,%.6f,%.6f,%.6f,"
                "%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
                "%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
                "%.6f,%.6f\n",
                r->model,
                r->model_label,
                r->platform,
                r->platform_label,
                r->context_tokens,
                r->reference_tokens_per_s,
                r->simulated_tokens_per_s,
                r->relative_error_pct,
                r->weight_bytes,
                r->attention_cache_bytes,
                r->attention_ops,
                r->tpot_ms,
                r->weight_stage_ms,
                r->attention_cache_ms,
                r->attention_compute_ms,
                r->tile_height,
                r->tile_width,
                r->alpha_read_compute,
                r->effective_pipeline_efficiency,
                r->read_compute_requests,
                r->npu_read_requests,
                r->npu_read_slices,
                r->controller_commands,
                r->read_compute_channel_rate_pct,
                r->ifc_read_compute_path_ms,
                r->npu_weight_read_path_ms,
                r->controller_weight_stage_ms,
                r->controller_balance_delta_pct,
                r->no_read_slicing_tokens_per_s,
                r->no_tiling_tokens_per_s,
                r->speedup_vs_no_read_slicing,
                r->speedup_vs_no_tiling);
    }
    return fclose(file);
}

static int write_json(const char *path, const IfcSimulationRow rows[IFC_ROW_COUNT], const IfcSummary *summary) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    fprintf(file, "{\n");
    fprintf(file, "  \"summary\": {\n");
    fprintf(file, "    \"row_count\": %d,\n", summary->row_count);
    fprintf(file, "    \"mean_abs_relative_error_pct\": %.3f,\n", summary->mean_abs_relative_error_pct);
    fprintf(file, "    \"max_abs_relative_error_pct\": %.3f,\n", summary->max_abs_relative_error_pct);
    fprintf(file, "    \"mean_relative_error_pct\": %.3f,\n", summary->mean_relative_error_pct);
    fprintf(file, "    \"worst_case_model\": \"%s\",\n", summary->worst_case_model);
    fprintf(file, "    \"worst_case_platform\": \"%s\"\n", summary->worst_case_platform);
    fprintf(file, "  },\n");
    fprintf(file, "  \"rows\": [\n");
    for (int i = 0; i < IFC_ROW_COUNT; ++i) {
        const IfcSimulationRow *r = &rows[i];
        fprintf(file,
                "    {\"model\": \"%s\", \"platform\": \"%s\", \"reference_tokens_per_s\": %.6f, "
                "\"simulated_tokens_per_s\": %.6f, \"relative_error_pct\": %.3f, "
                "\"tpot_ms\": %.6f, \"tile_height\": %.6f, \"tile_width\": %.6f, "
                "\"alpha_read_compute\": %.6f, \"controller_commands\": %.6f}%s\n",
                r->model,
                r->platform,
                r->reference_tokens_per_s,
                r->simulated_tokens_per_s,
                r->relative_error_pct,
                r->tpot_ms,
                r->tile_height,
                r->tile_width,
                r->alpha_read_compute,
                r->controller_commands,
                i == IFC_ROW_COUNT - 1 ? "" : ",");
    }
    fprintf(file, "  ]\n");
    fprintf(file, "}\n");
    return fclose(file);
}

static int write_request_trace(const char *path, const IfcSimulationRow rows[IFC_ROW_COUNT]) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    fprintf(file, "model,platform,opcode,logical_requests,slices,total_commands,path_ms,notes\n");
    for (int i = 0; i < IFC_ROW_COUNT; ++i) {
        const IfcSimulationRow *r = &rows[i];
        fprintf(file,
                "%s,%s,%s,%.6f,0,%.6f,%.6f,flash-side tiled weight read-compute\n",
                r->model,
                r->platform,
                ifc_opcode_name(IFC_OP_READ_COMPUTE),
                r->read_compute_requests,
                r->read_compute_requests,
                r->ifc_read_compute_path_ms);
        fprintf(file,
                "%s,%s,%s,%.6f,%.6f,%.6f,%.6f,NPU-side weight reads carried as sliced channel transfers\n",
                r->model,
                r->platform,
                ifc_opcode_name(IFC_OP_READ_SLICE),
                r->npu_read_requests,
                r->npu_read_slices,
                r->npu_read_slices,
                r->npu_weight_read_path_ms);
    }
    return fclose(file);
}

static int write_ablation_summary(const char *path, const IfcSimulationRow rows[IFC_ROW_COUNT]) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    fprintf(file,
            "model,platform,full_tokens_per_s,no_read_slicing_tokens_per_s,no_tiling_tokens_per_s,"
            "speedup_vs_no_read_slicing,speedup_vs_no_tiling\n");
    for (int i = 0; i < IFC_ROW_COUNT; ++i) {
        const IfcSimulationRow *r = &rows[i];
        fprintf(file,
                "%s,%s,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                r->model,
                r->platform,
                r->simulated_tokens_per_s,
                r->no_read_slicing_tokens_per_s,
                r->no_tiling_tokens_per_s,
                r->speedup_vs_no_read_slicing,
                r->speedup_vs_no_tiling);
    }
    return fclose(file);
}

static int write_controller_timing_summary(const char *path, const IfcSimulationRow rows[IFC_ROW_COUNT]) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    fprintf(file,
            "model,platform,read_compute_requests,npu_read_requests,npu_read_slices,total_controller_commands,"
            "read_compute_path_ms,read_slice_path_ms,controller_stage_ms,balance_delta_pct,read_compute_channel_rate_pct\n");
    for (int i = 0; i < IFC_ROW_COUNT; ++i) {
        const IfcSimulationRow *r = &rows[i];
        fprintf(file,
                "%s,%s,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                r->model,
                r->platform,
                r->read_compute_requests,
                r->npu_read_requests,
                r->npu_read_slices,
                r->controller_commands,
                r->ifc_read_compute_path_ms,
                r->npu_weight_read_path_ms,
                r->controller_weight_stage_ms,
                r->controller_balance_delta_pct,
                r->read_compute_channel_rate_pct);
    }
    return fclose(file);
}

static int write_npu_timing(const char *path, const IfcSimulationRow rows[IFC_ROW_COUNT]) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    fprintf(file,
            "model,platform,attention_cache_bytes,attention_ops,dram_attention_ms,npu_attention_compute_ms,"
            "controller_weight_stage_ms,tpot_ms,tpot_reconstructed_ms\n");
    for (int i = 0; i < IFC_ROW_COUNT; ++i) {
        const IfcSimulationRow *r = &rows[i];
        double reconstructed = r->controller_weight_stage_ms + r->attention_cache_ms + r->attention_compute_ms;
        fprintf(file,
                "%s,%s,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                r->model,
                r->platform,
                r->attention_cache_bytes,
                r->attention_ops,
                r->attention_cache_ms,
                r->attention_compute_ms,
                r->controller_weight_stage_ms,
                r->tpot_ms,
                reconstructed);
    }
    return fclose(file);
}

static int write_latency_breakdown(const char *path, const IfcSimulationRow rows[IFC_ROW_COUNT]) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    fprintf(file,
            "model,platform,operator_group,mapped_engine,work_metric,work_value,latency_ms,"
            "overlap_role,notes\n");
    for (int i = 0; i < IFC_ROW_COUNT; ++i) {
        const IfcSimulationRow *r = &rows[i];
        fprintf(file,
                "%s,%s,flash_weight_gemv,IFC_READ_COMPUTE,read_compute_requests,%.6f,%.6f,overlapped_path,"
                "tiled weight GeMV handled by in-flash read-compute\n",
                r->model,
                r->platform,
                r->read_compute_requests,
                r->ifc_read_compute_path_ms);
        fprintf(file,
                "%s,%s,flash_weight_slice_transfer,NPU_READ_SLICE,read_slices,%.6f,%.6f,overlapped_path,"
                "sliced channel transfers for the non-read-compute weight fraction\n",
                r->model,
                r->platform,
                r->npu_read_slices,
                r->npu_weight_read_path_ms);
        fprintf(file,
                "%s,%s,effective_weight_stage,controller_overlap,max_path_after_efficiency,%.6f,%.6f,selected_path,"
                "max(read-compute path, sliced-read path) after platform efficiency\n",
                r->model,
                r->platform,
                r->controller_weight_stage_ms,
                r->controller_weight_stage_ms);
        fprintf(file,
                "%s,%s,attention_state_memory,NPU_DRAM,attention_cache_bytes,%.6f,%.6f,sequential_tail,"
                "DRAM traffic term for decode-time attention state access\n",
                r->model,
                r->platform,
                r->attention_cache_bytes,
                r->attention_cache_ms);
        fprintf(file,
                "%s,%s,attention_score_value_compute,NPU,attention_ops,%.6f,%.6f,sequential_tail,"
                "NPU arithmetic term for decode-time attention score/value work\n",
                r->model,
                r->platform,
                r->attention_ops,
                r->attention_compute_ms);
        fprintf(file,
                "%s,%s,total_tpot,combined,tpot_ms,%.6f,%.6f,total,"
                "effective_weight_stage + attention_state_memory + attention_score_value_compute\n",
                r->model,
                r->platform,
                r->tpot_ms,
                r->tpot_ms);
    }
    return fclose(file);
}

static int write_figure12_read_slice_ablation(const char *path, const IfcConfig *config, const IfcSimulationRow rows[IFC_ROW_COUNT]) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    fprintf(file, "model,platform,full_tokens_per_s,without_read_slicing_tokens_per_s,speedup,paper_text_range\n");
    for (int i = 0; i < IFC_ROW_COUNT; ++i) {
        const IfcSimulationRow *r = &rows[i];
        if (strcmp(r->platform, config->platforms[0].name) != 0) {
            continue;
        }
        fprintf(file,
                "%s,%s,%.6f,%.6f,%.6f,1.6x-1.8x\n",
                r->model,
                r->platform,
                r->simulated_tokens_per_s,
                r->no_read_slicing_tokens_per_s,
                r->speedup_vs_no_read_slicing);
    }
    return fclose(file);
}

static int write_figure14_tiling_ablation(const char *path, const IfcConfig *config, const IfcSimulationRow rows[IFC_ROW_COUNT]) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    fprintf(file, "model,platform,full_tokens_per_s,without_hardware_aware_tiling_tokens_per_s,speedup,paper_text_range\n");
    for (int i = 0; i < IFC_ROW_COUNT; ++i) {
        const IfcSimulationRow *r = &rows[i];
        if (strcmp(r->platform, config->platforms[0].name) != 0) {
            continue;
        }
        fprintf(file,
                "%s,%s,%.6f,%.6f,%.6f,1.3x-1.4x\n",
                r->model,
                r->platform,
                r->simulated_tokens_per_s,
                r->no_tiling_tokens_per_s,
                r->speedup_vs_no_tiling);
    }
    return fclose(file);
}

static int write_report(const char *path, const IfcConfig *config, const IfcSimulationRow rows[IFC_ROW_COUNT], const IfcSummary *summary) {
    FILE *file = fopen(path, "w");
    IfcTileModel first_tile;
    if (file == NULL) {
        return -1;
    }
    first_tile = ifc_derive_tile_model(&config->platforms[0]);
    fprintf(file, "# Figure 9 Reproduction Report\n\n");
    fprintf(file, "This report compares the standalone C IFC simulator against the Cambricon-LLM Figure 9 W8A8 decode-speed points. The simulator includes a C NPU timing path, an SSDsim-inspired flash resource timeline, and a cycle-stepped command trace with extended READ_COMPUTE and READ_SLICE commands.\n\n");
    fprintf(file, "## Summary\n\n");
    fprintf(file, "- Rows: %d\n", summary->row_count);
    fprintf(file, "- Mean absolute relative error: %.3f%%\n", summary->mean_abs_relative_error_pct);
    fprintf(file, "- Max absolute relative error: %.3f%%\n", summary->max_abs_relative_error_pct);
    fprintf(file, "- Worst case: %s on %s\n\n", summary->worst_case_model, summary->worst_case_platform);
    fprintf(file, "## Comparison\n\n");
    fprintf(file, "| Model | Platform | Paper token/s | Sim token/s | Error | TPOT ms | Tile HxW | Alpha | Commands |\n");
    fprintf(file, "|---|---|---:|---:|---:|---:|---:|---:|---:|\n");
    for (int i = 0; i < IFC_ROW_COUNT; ++i) {
        const IfcSimulationRow *r = &rows[i];
        fprintf(file,
                "| %s | %s | %.3f | %.3f | %+.2f%% | %.3f | %.0fx%.0f | %.3f | %.0f |\n",
                r->model_label,
                r->platform_label,
                r->reference_tokens_per_s,
                r->simulated_tokens_per_s,
                r->relative_error_pct,
                r->tpot_ms,
                r->tile_height,
                r->tile_width,
                r->alpha_read_compute,
                r->controller_commands);
    }
    fprintf(file, "\n## Controller Artifacts\n\n");
    fprintf(file, "- `request_trace.csv` records aggregate READ_COMPUTE and READ_SLICE command counts for every Figure 9 row.\n");
    fprintf(file, "- `controller_timing_summary.csv` records controller-derived READ_COMPUTE/READ_SLICE timing balance for every row.\n");
    fprintf(file, "- `npu_timing.csv` records DRAM attention-cache traffic and NPU attention arithmetic timing for every row.\n");
    fprintf(file, "- `latency_breakdown.csv` maps each row to operator groups and reconstructs TPOT.\n");
    fprintf(file, "- `controller_schedule.csv` records one %s/%s event-timeline sample with channel/chip/die/plane placement and busy intervals.\n", config->models[0].label, config->platforms[0].label);
    fprintf(file, "- `cycle_controller_trace.csv` records a C cycle-stepped command trace for the first configured platform.\n");
    fprintf(file, "- `cycle_controller_stats.csv` records cycle-level resource statistics for the same command stream.\n");
    fprintf(file, "- `ablation_summary.csv` records no-read-slicing and no-tiling speed comparisons for the Figure 12/Figure 14 style checks.\n");
    fprintf(file, "- `figure12_read_slice_ablation.csv` and `figure14_tiling_ablation.csv` expose Cambricon-LLM-S specific ablation checks against the paper text ranges.\n");
    fprintf(file, "- `platform_summary.csv` and `model_summary.csv` aggregate reproduction error and throughput by platform/model.\n");
    fprintf(file, "- `tile_profile.csv` records derived tile dimensions, request timings, and read-compute channel occupancy.\n");
    fprintf(file, "- `system_profile.csv` records effective context length, NPU throughput, and DRAM bandwidth.\n");
    fprintf(file, "- `reproduction_checks.csv` records pass/fail checks for row count, error bounds, tile size, ablation ranges, and controller balance.\n");
    fprintf(file, "- READ_SLICE channel intervals are emitted between READ_COMPUTE submissions to model the paper's sliced read behavior. This artifact is a command-level controller audit, not a claim of line-by-line equivalence with the private SSDsim fork used by the paper authors.\n");
    fprintf(file, "\n## Plots\n\n");
    fprintf(file, "- `figures/figure9_decode_speed.svg` compares paper Figure 9 decode speed against the C simulator for all 21 points.\n");
    fprintf(file, "- `figures/figure9_relative_error.svg` shows signed Figure 9 error with +/-15%% reproduction bounds.\n");
    fprintf(file, "- `figures/platform_error_summary.svg` summarizes mean and max absolute error per platform.\n");
    fprintf(file, "- `figures/controller_schedule_timeline.svg` visualizes the first configured platform's sample controller schedule.\n");
    fprintf(file, "- `figures/figure12_read_slice_ablation.svg` plots the first configured platform's read-slicing ablation.\n");
    fprintf(file, "- `figures/figure14_tiling_ablation.svg` plots the first configured platform's hardware-aware tiling ablation.\n");
    fprintf(file, "\n## Sanity Checks\n\n");
    fprintf(file, "- First configured platform `%s` derives a %.0fx%.0f tile.\n", config->platforms[0].name, first_tile.tile_height, first_tile.tile_width);
    fprintf(file, "- Read-compute workload fraction and channel occupancy are emitted for every model/platform row.\n");
    fprintf(file, "- No-read-slicing and no-tiling rows are produced as controlled ablations from the same C model path.\n");
    return fclose(file);
}

int ifc_write_outputs(const char *output_dir, const IfcSimulationRow rows[IFC_ROW_COUNT], const IfcSummary *summary) {
    IfcConfig config;
    ifc_config_init_default(&config);
    return ifc_write_outputs_config(output_dir, &config, rows, summary);
}

int ifc_write_outputs_config(const char *output_dir, const IfcConfig *config, const IfcSimulationRow rows[IFC_ROW_COUNT], const IfcSummary *summary) {
    char csv_path[4096];
    char json_path[4096];
    char report_path[4096];
    char request_trace_path[4096];
    char controller_schedule_path[4096];
    char cycle_controller_trace_path[4096];
    char cycle_controller_stats_path[4096];
    char ablation_summary_path[4096];
    char controller_timing_path[4096];
    char npu_timing_path[4096];
    char latency_breakdown_path[4096];
    char figure12_path[4096];
    char figure14_path[4096];

    if (ensure_dir(output_dir) != 0) {
        return -1;
    }
    join_path(csv_path, sizeof(csv_path), output_dir, "figure9_reproduction.csv");
    join_path(json_path, sizeof(json_path), output_dir, "summary.json");
    join_path(report_path, sizeof(report_path), output_dir, "report.md");
    join_path(request_trace_path, sizeof(request_trace_path), output_dir, "request_trace.csv");
    join_path(controller_schedule_path, sizeof(controller_schedule_path), output_dir, "controller_schedule.csv");
    join_path(cycle_controller_trace_path, sizeof(cycle_controller_trace_path), output_dir, "cycle_controller_trace.csv");
    join_path(cycle_controller_stats_path, sizeof(cycle_controller_stats_path), output_dir, "cycle_controller_stats.csv");
    join_path(ablation_summary_path, sizeof(ablation_summary_path), output_dir, "ablation_summary.csv");
    join_path(controller_timing_path, sizeof(controller_timing_path), output_dir, "controller_timing_summary.csv");
    join_path(npu_timing_path, sizeof(npu_timing_path), output_dir, "npu_timing.csv");
    join_path(latency_breakdown_path, sizeof(latency_breakdown_path), output_dir, "latency_breakdown.csv");
    join_path(figure12_path, sizeof(figure12_path), output_dir, "figure12_read_slice_ablation.csv");
    join_path(figure14_path, sizeof(figure14_path), output_dir, "figure14_tiling_ablation.csv");

    if (write_csv(csv_path, rows) != 0) {
        return -1;
    }
    if (write_json(json_path, rows, summary) != 0) {
        return -1;
    }
    if (write_report(report_path, config, rows, summary) != 0) {
        return -1;
    }
    if (write_request_trace(request_trace_path, rows) != 0) {
        return -1;
    }
    if (ifc_write_sample_controller_schedule_for_platform(controller_schedule_path, &config->platforms[0]) != 0) {
        return -1;
    }
    if (ifc_write_cycle_controller_trace_for_platform(
            cycle_controller_trace_path,
            cycle_controller_stats_path,
            &config->platforms[0]) != 0) {
        return -1;
    }
    if (write_ablation_summary(ablation_summary_path, rows) != 0) {
        return -1;
    }
    if (write_controller_timing_summary(controller_timing_path, rows) != 0) {
        return -1;
    }
    if (write_npu_timing(npu_timing_path, rows) != 0) {
        return -1;
    }
    if (write_latency_breakdown(latency_breakdown_path, rows) != 0) {
        return -1;
    }
    if (write_figure12_read_slice_ablation(figure12_path, config, rows) != 0) {
        return -1;
    }
    if (write_figure14_tiling_ablation(figure14_path, config, rows) != 0) {
        return -1;
    }
    return 0;
}
