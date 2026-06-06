#include "ifc_cambricon_llm.h"

#include <stdio.h>
#include <string.h>

static const char *parse_output_dir(int argc, char **argv) {
    const char *output_dir = "results";
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--output-dir") == 0 && i + 1 < argc) {
            output_dir = argv[i + 1];
            ++i;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("usage: ifc_cambricon_llm [--output-dir DIR]\n");
            return NULL;
        } else {
            fprintf(stderr, "error: unknown argument: %s\n", argv[i]);
            return NULL;
        }
    }
    return output_dir;
}

int main(int argc, char **argv) {
    const char *output_dir = parse_output_dir(argc, argv);
    if (output_dir == NULL) {
        return argc > 1 ? 1 : 0;
    }

    IfcSimulationRow rows[IFC_ROW_COUNT];
    IfcSummary summary = ifc_simulate_reproduction(rows, 1000);
    if (ifc_write_outputs(output_dir, rows, &summary) != 0) {
        fprintf(stderr, "error: failed to write outputs to %s\n", output_dir);
        return 1;
    }
    if (ifc_write_analysis_outputs(output_dir, rows, &summary) != 0) {
        fprintf(stderr, "error: failed to write analysis outputs to %s\n", output_dir);
        return 1;
    }
    if (ifc_write_plots(output_dir, rows, &summary) != 0) {
        fprintf(stderr, "error: failed to write plots to %s\n", output_dir);
        return 1;
    }

    printf("passed: Cambricon-LLM Figure 9 C reproduction\n");
    printf("metrics_csv: %s/figure9_reproduction.csv\n", output_dir);
    printf("summary_json: %s/summary.json\n", output_dir);
    printf("report_md: %s/report.md\n", output_dir);
    printf("request_trace_csv: %s/request_trace.csv\n", output_dir);
    printf("controller_timing_csv: %s/controller_timing_summary.csv\n", output_dir);
    printf("npu_timing_csv: %s/npu_timing.csv\n", output_dir);
    printf("controller_schedule_csv: %s/controller_schedule.csv\n", output_dir);
    printf("ablation_summary_csv: %s/ablation_summary.csv\n", output_dir);
    printf("figure12_read_slice_csv: %s/figure12_read_slice_ablation.csv\n", output_dir);
    printf("figure14_tiling_csv: %s/figure14_tiling_ablation.csv\n", output_dir);
    printf("platform_summary_csv: %s/platform_summary.csv\n", output_dir);
    printf("model_summary_csv: %s/model_summary.csv\n", output_dir);
    printf("tile_profile_csv: %s/tile_profile.csv\n", output_dir);
    printf("reproduction_checks_csv: %s/reproduction_checks.csv\n", output_dir);
    printf("figure9_svg: %s/figures/figure9_decode_speed.svg\n", output_dir);
    printf("figure9_error_svg: %s/figures/figure9_relative_error.svg\n", output_dir);
    printf("platform_summary_svg: %s/figures/platform_error_summary.svg\n", output_dir);
    printf("controller_timeline_svg: %s/figures/controller_schedule_timeline.svg\n", output_dir);
    printf("figure12_svg: %s/figures/figure12_read_slice_ablation.svg\n", output_dir);
    printf("figure14_svg: %s/figures/figure14_tiling_ablation.svg\n", output_dir);
    printf("rows: %d\n", summary.row_count);
    printf("mean_abs_relative_error_pct: %.3f\n", summary.mean_abs_relative_error_pct);
    printf("max_abs_relative_error_pct: %.3f\n", summary.max_abs_relative_error_pct);
    return 0;
}
