#include "ifc_cambricon_llm.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void require_true(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "failed: %s\n", message);
        exit(1);
    }
}

static void require_close(double actual, double expected, double tolerance, const char *message) {
    if (fabs(actual - expected) > tolerance) {
        fprintf(stderr, "failed: %s actual=%.9f expected=%.9f tolerance=%.9f\n", message, actual, expected, tolerance);
        exit(1);
    }
}

static void require_nonempty_file(const char *path) {
    struct stat info;
    if (stat(path, &info) != 0 || info.st_size <= 0) {
        fprintf(stderr, "failed: expected nonempty file %s\n", path);
        exit(1);
    }
}

int main(void) {
    IfcTileModel tile = ifc_derive_tile_model(&IFC_PLATFORMS[0]);
    require_close(tile.tile_height, 256.0, 1e-9, "Cambricon-LLM-S tile height");
    require_close(tile.tile_width, 2048.0, 1e-9, "Cambricon-LLM-S tile width");
    require_true(tile.alpha_read_compute > 0.35 && tile.alpha_read_compute < 0.36, "alpha range");
    require_true(strcmp(ifc_opcode_name(IFC_OP_READ), "READ") == 0, "READ opcode");
    require_true(strcmp(ifc_opcode_name(IFC_OP_WRITE), "WRITE") == 0, "WRITE opcode");
    require_true(strcmp(ifc_opcode_name(IFC_OP_READ_COMPUTE), "READ_COMPUTE") == 0, "READ_COMPUTE opcode");
    require_true(strcmp(ifc_opcode_name(IFC_OP_READ_SLICE), "READ_SLICE") == 0, "READ_SLICE opcode");

    IfcSimulationRow rows[IFC_ROW_COUNT];
    IfcSummary summary = ifc_simulate_reproduction(rows, 1000);
    require_true(summary.row_count == 21, "row count");
    require_true(summary.max_abs_relative_error_pct <= 15.0, "max reproduction error");
    require_true(summary.mean_abs_relative_error_pct <= 9.0, "mean reproduction error");

    for (int i = 0; i < IFC_ROW_COUNT; ++i) {
        require_true(rows[i].read_compute_requests > 0.0, "read-compute command count");
        require_true(rows[i].npu_read_requests > 0.0, "sliced read logical count");
        require_close(
            rows[i].npu_read_slices,
            rows[i].npu_read_requests * (double)IFC_READ_SLICES_PER_REQUEST,
            1e-6,
            "read slice expansion");
        require_close(
            rows[i].controller_commands,
            rows[i].read_compute_requests + rows[i].npu_read_slices,
            1e-6,
            "controller command total");
        require_true(rows[i].controller_weight_stage_ms > 0.0, "controller stage time");
        require_true(rows[i].weight_bytes > 0.0, "weight bytes");
        require_true(rows[i].attention_cache_bytes > 0.0, "attention cache bytes");
        require_true(rows[i].attention_ops > 0.0, "attention ops");
        require_true(rows[i].controller_balance_delta_pct < 1e-6, "controller read-compute/read-slice balance");
        require_close(
            rows[i].tpot_ms,
            rows[i].controller_weight_stage_ms + rows[i].attention_cache_ms + rows[i].attention_compute_ms,
            1e-6,
            "TPOT decomposition");
        require_true(rows[i].speedup_vs_no_read_slicing > 1.55, "read slicing lower speedup bound");
        require_true(rows[i].speedup_vs_no_read_slicing < 1.75, "read slicing upper speedup bound");
        require_true(rows[i].speedup_vs_no_tiling > 1.25, "tiling lower speedup bound");
        require_true(rows[i].speedup_vs_no_tiling < 1.36, "tiling upper speedup bound");
    }

    const char *artifact_dir = "/tmp/ifc_cambricon_llm_test_outputs";
    require_true(ifc_write_outputs(artifact_dir, rows, &summary) == 0, "write primary outputs");
    require_true(ifc_write_analysis_outputs(artifact_dir, rows, &summary) == 0, "write analysis outputs");
    require_true(ifc_write_plots(artifact_dir, rows, &summary) == 0, "write plot outputs");
    require_nonempty_file("/tmp/ifc_cambricon_llm_test_outputs/figure9_reproduction.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_test_outputs/platform_summary.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_test_outputs/latency_breakdown.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_test_outputs/reproduction_checks.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_test_outputs/system_profile.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_test_outputs/figures/figure9_decode_speed.svg");
    require_nonempty_file("/tmp/ifc_cambricon_llm_test_outputs/figures/figure9_relative_error.svg");
    require_nonempty_file("/tmp/ifc_cambricon_llm_test_outputs/figures/controller_schedule_timeline.svg");

    IfcConfig custom_config;
    IfcSimulationRow custom_rows[IFC_ROW_COUNT];
    char config_error[256];
    ifc_config_init_default(&custom_config);
    require_true(
        ifc_config_load_platforms_csv(&custom_config, "configs/example_scaled_platforms.csv", config_error, sizeof(config_error)) == 0,
        "load scaled platform CSV");
    require_true(
        ifc_config_load_system_csv(&custom_config, "configs/example_system_fast_npu.csv", config_error, sizeof(config_error)) == 0,
        "load system CSV");
    require_true(
        ifc_config_load_models_csv(&custom_config, "configs/example_models_mixed.csv", config_error, sizeof(config_error)) == 0,
        "load model CSV");
    IfcSummary custom_summary = ifc_simulate_config(&custom_config, custom_rows);
    require_true(custom_summary.row_count == 21, "custom config row count");
    require_true(strcmp(custom_rows[0].platform, "ifc_s_1600") == 0, "custom platform name");
    require_true(strcmp(custom_rows[0].model, "small_7b") == 0, "custom model name");
    require_true(fabs(custom_rows[0].simulated_tokens_per_s - rows[0].simulated_tokens_per_s) > 1e-4, "custom config changes throughput");
    require_true(ifc_write_outputs_config("/tmp/ifc_cambricon_llm_custom_outputs", &custom_config, custom_rows, &custom_summary) == 0, "write custom outputs");
    require_true(ifc_write_analysis_outputs_config("/tmp/ifc_cambricon_llm_custom_outputs", &custom_config, custom_rows, &custom_summary) == 0, "write custom analysis outputs");
    require_true(ifc_write_plots_config("/tmp/ifc_cambricon_llm_custom_outputs", &custom_config, custom_rows, &custom_summary) == 0, "write custom plot outputs");
    require_nonempty_file("/tmp/ifc_cambricon_llm_custom_outputs/tile_profile.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_custom_outputs/latency_breakdown.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_custom_outputs/system_profile.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_custom_outputs/figures/controller_schedule_timeline.svg");

    printf("passed: C simulator tests\n");
    return 0;
}
