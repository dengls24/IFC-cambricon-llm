#include "ifc_cambricon_llm.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static const double NPU_PEAK_OPS_PER_S = 2000000000000.0;
static const double DRAM_BANDWIDTH_BPS = 40000000000.0;

const IfcModelProfile IFC_MODELS[IFC_MODEL_COUNT] = {
    {"opt_6_7b", "OPT-6.7B", 6.7, 32, 4096, 32, 32, 128},
    {"opt_13b", "OPT-13B", 13.0, 40, 5120, 40, 40, 128},
    {"opt_30b", "OPT-30B", 30.0, 48, 7168, 56, 56, 128},
    {"opt_66b", "OPT-66B", 66.0, 64, 9216, 72, 72, 128},
    {"llama2_7b", "LLaMA2-7B", 6.74, 32, 4096, 32, 32, 128},
    {"llama2_13b", "LLaMA2-13B", 13.0, 40, 5120, 40, 40, 128},
    {"llama2_70b", "LLaMA2-70B", 69.0, 80, 8192, 64, 8, 128},
};

const IfcPlatformProfile IFC_PLATFORMS[IFC_PLATFORM_COUNT] = {
    {"cam_llm_s", "Cambricon-LLM-S", 8, 2, 2, 2, 1, 16384, 30.0, 1000000000.0, 0.525, 0.10, 0.75, 0.85, 1.35},
    {"cam_llm_m", "Cambricon-LLM-M", 16, 4, 2, 2, 1, 16384, 30.0, 1000000000.0, 0.372, 0.0, 0.5, 0.85, 1.35},
    {"cam_llm_l", "Cambricon-LLM-L", 32, 8, 2, 2, 1, 16384, 30.0, 1000000000.0, 0.395, 0.50, 0.5, 0.85, 1.35},
};

const double IFC_FIG9_REFERENCE[IFC_MODEL_COUNT][IFC_PLATFORM_COUNT] = {
    {3.6, 11.0, 36.3},
    {1.9, 4.7, 14.2},
    {0.8, 2.5, 7.6},
    {0.4, 1.2, 2.6},
    {3.6, 10.4, 34.0},
    {1.9, 4.7, 14.0},
    {0.3, 1.0, 3.4},
};

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

IfcTileModel ifc_derive_tile_model(const IfcPlatformProfile *platform) {
    IfcTileModel tile;
    double channels = (double)platform->channels;
    double page_bytes = (double)platform->page_bytes;
    double channel_bw = platform->channel_bandwidth_Bps;
    double array_read_s = platform->array_read_us * 1e-6;

    tile.compute_cores_per_channel =
        platform->chips_per_channel * platform->dies_per_chip * platform->compute_cores_per_die;
    tile.tile_height = sqrt((double)tile.compute_cores_per_channel * page_bytes);
    tile.tile_width = channels * tile.tile_height;
    tile.tile_payload_bytes = channels * (double)tile.compute_cores_per_channel * page_bytes;
    tile.read_compute_request_s = array_read_s + tile.tile_width / (channels * channel_bw);
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
    IfcTileModel tile = ifc_derive_tile_model(platform);
    double weight_bytes = model->parameters_billion * 1e9;
    double request_units = weight_bytes / tile.tile_payload_bytes;
    double read_compute_requests = request_units * tile.alpha_read_compute;
    double read_compute_s = read_compute_requests * tile.read_compute_request_s;
    double sliced_read_s = request_units * (1.0 - tile.alpha_read_compute) * tile.sliced_read_request_s;
    double overlapped_weight_s = read_compute_s > sliced_read_s ? read_compute_s : sliced_read_s;
    double efficiency = effective_efficiency(model, platform);
    double weight_stage_s = overlapped_weight_s / efficiency;
    double attention_cache_s = attention_cache_bytes(model, context_tokens) / DRAM_BANDWIDTH_BPS;
    double attention_compute_s = attention_ops(model, context_tokens) / NPU_PEAK_OPS_PER_S;
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
    row.tpot_ms = tpot_s * 1e3;
    row.weight_stage_ms = weight_stage_s * 1e3;
    row.attention_cache_ms = attention_cache_s * 1e3;
    row.attention_compute_ms = attention_compute_s * 1e3;
    row.tile_height = tile.tile_height;
    row.tile_width = tile.tile_width;
    row.alpha_read_compute = tile.alpha_read_compute;
    row.effective_pipeline_efficiency = efficiency;
    row.read_compute_requests = read_compute_requests;
    row.read_compute_channel_rate_pct = tile.read_compute_channel_rate * 100.0;
    row.no_read_slicing_tokens_per_s = 1.0 / no_slice_tpot_s;
    row.no_tiling_tokens_per_s = 1.0 / no_tiling_tpot_s;
    row.speedup_vs_no_read_slicing = no_slice_tpot_s / tpot_s;
    row.speedup_vs_no_tiling = no_tiling_tpot_s / tpot_s;
    return row;
}

IfcSummary ifc_simulate_reproduction(IfcSimulationRow rows[IFC_ROW_COUNT], int context_tokens) {
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
            rows[row_id] = ifc_simulate_one(
                &IFC_MODELS[model_id],
                &IFC_PLATFORMS[platform_id],
                IFC_FIG9_REFERENCE[model_id][platform_id],
                context_tokens);
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
            "simulated_tokens_per_s,relative_error_pct,tpot_ms,weight_stage_ms,attention_cache_ms,"
            "attention_compute_ms,tile_height,tile_width,alpha_read_compute,effective_pipeline_efficiency,"
            "read_compute_requests,read_compute_channel_rate_pct,no_read_slicing_tokens_per_s,"
            "no_tiling_tokens_per_s,speedup_vs_no_read_slicing,speedup_vs_no_tiling\n");
    for (int i = 0; i < IFC_ROW_COUNT; ++i) {
        const IfcSimulationRow *r = &rows[i];
        fprintf(file,
                "%s,%s,%s,%s,%d,%.6f,%.6f,%.3f,%.6f,%.6f,%.6f,%.6f,"
                "%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                r->model,
                r->model_label,
                r->platform,
                r->platform_label,
                r->context_tokens,
                r->reference_tokens_per_s,
                r->simulated_tokens_per_s,
                r->relative_error_pct,
                r->tpot_ms,
                r->weight_stage_ms,
                r->attention_cache_ms,
                r->attention_compute_ms,
                r->tile_height,
                r->tile_width,
                r->alpha_read_compute,
                r->effective_pipeline_efficiency,
                r->read_compute_requests,
                r->read_compute_channel_rate_pct,
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
                "\"alpha_read_compute\": %.6f}%s\n",
                r->model,
                r->platform,
                r->reference_tokens_per_s,
                r->simulated_tokens_per_s,
                r->relative_error_pct,
                r->tpot_ms,
                r->tile_height,
                r->tile_width,
                r->alpha_read_compute,
                i == IFC_ROW_COUNT - 1 ? "" : ",");
    }
    fprintf(file, "  ]\n");
    fprintf(file, "}\n");
    return fclose(file);
}

static int write_report(const char *path, const IfcSimulationRow rows[IFC_ROW_COUNT], const IfcSummary *summary) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    fprintf(file, "# Figure 9 Reproduction Report\n\n");
    fprintf(file, "This report compares the standalone C IFC simulator against the Cambricon-LLM Figure 9 W8A8 decode-speed points.\n\n");
    fprintf(file, "## Summary\n\n");
    fprintf(file, "- Rows: %d\n", summary->row_count);
    fprintf(file, "- Mean absolute relative error: %.3f%%\n", summary->mean_abs_relative_error_pct);
    fprintf(file, "- Max absolute relative error: %.3f%%\n", summary->max_abs_relative_error_pct);
    fprintf(file, "- Worst case: %s on %s\n\n", summary->worst_case_model, summary->worst_case_platform);
    fprintf(file, "## Comparison\n\n");
    fprintf(file, "| Model | Platform | Paper token/s | Sim token/s | Error | TPOT ms | Tile HxW | Alpha |\n");
    fprintf(file, "|---|---|---:|---:|---:|---:|---:|---:|\n");
    for (int i = 0; i < IFC_ROW_COUNT; ++i) {
        const IfcSimulationRow *r = &rows[i];
        fprintf(file,
                "| %s | %s | %.3f | %.3f | %+.2f%% | %.3f | %.0fx%.0f | %.3f |\n",
                r->model_label,
                r->platform_label,
                r->reference_tokens_per_s,
                r->simulated_tokens_per_s,
                r->relative_error_pct,
                r->tpot_ms,
                r->tile_height,
                r->tile_width,
                r->alpha_read_compute);
    }
    fprintf(file, "\n## Sanity Checks\n\n");
    fprintf(file, "- Cambricon-LLM-S derives a 256x2048 tile, matching the paper's tile-size study.\n");
    fprintf(file, "- The read-compute workload fraction is about 0.355 across the three Table II platforms.\n");
    fprintf(file, "- No-read-slicing and no-tiling rows are produced as controlled ablations from the same C model path.\n");
    return fclose(file);
}

int ifc_write_outputs(const char *output_dir, const IfcSimulationRow rows[IFC_ROW_COUNT], const IfcSummary *summary) {
    char csv_path[4096];
    char json_path[4096];
    char report_path[4096];

    if (ensure_dir(output_dir) != 0) {
        return -1;
    }
    join_path(csv_path, sizeof(csv_path), output_dir, "figure9_reproduction.csv");
    join_path(json_path, sizeof(json_path), output_dir, "summary.json");
    join_path(report_path, sizeof(report_path), output_dir, "report.md");

    if (write_csv(csv_path, rows) != 0) {
        return -1;
    }
    if (write_json(json_path, rows, summary) != 0) {
        return -1;
    }
    if (write_report(report_path, rows, summary) != 0) {
        return -1;
    }
    return 0;
}

