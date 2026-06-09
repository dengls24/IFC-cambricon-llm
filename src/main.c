#include "ifc_cambricon_llm.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *output_dir;
    const char *models_csv;
    const char *platforms_csv;
    const char *system_csv;
    const char *reference_csv;
    int show_help;
    int parse_error;
} CliOptions;

static void print_usage(void) {
    printf("usage: ifc_cambricon_llm [--output-dir DIR] [--models-csv FILE] [--platforms-csv FILE] [--system-csv FILE] [--reference-csv FILE]\n");
}

static CliOptions parse_options(int argc, char **argv) {
    CliOptions options;
    options.output_dir = "results";
    options.models_csv = NULL;
    options.platforms_csv = NULL;
    options.system_csv = NULL;
    options.reference_csv = NULL;
    options.show_help = 0;
    options.parse_error = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--output-dir") == 0 && i + 1 < argc) {
            options.output_dir = argv[i + 1];
            ++i;
        } else if (strcmp(argv[i], "--models-csv") == 0 && i + 1 < argc) {
            options.models_csv = argv[i + 1];
            ++i;
        } else if (strcmp(argv[i], "--platforms-csv") == 0 && i + 1 < argc) {
            options.platforms_csv = argv[i + 1];
            ++i;
        } else if (strcmp(argv[i], "--system-csv") == 0 && i + 1 < argc) {
            options.system_csv = argv[i + 1];
            ++i;
        } else if (strcmp(argv[i], "--reference-csv") == 0 && i + 1 < argc) {
            options.reference_csv = argv[i + 1];
            ++i;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            options.show_help = 1;
            return options;
        } else {
            fprintf(stderr, "error: unknown argument: %s\n", argv[i]);
            options.parse_error = 1;
            return options;
        }
    }
    return options;
}

int main(int argc, char **argv) {
    CliOptions options = parse_options(argc, argv);
    IfcConfig config;
    char config_error[256];

    if (options.show_help) {
        print_usage();
        return 0;
    }
    if (options.parse_error) {
        print_usage();
        return 1;
    }

    ifc_config_init_default(&config);
    if (options.models_csv != NULL &&
        ifc_config_load_models_csv(&config, options.models_csv, config_error, sizeof(config_error)) != 0) {
        fprintf(stderr, "error: failed to load models CSV %s: %s\n", options.models_csv, config_error);
        return 1;
    }
    if (options.platforms_csv != NULL &&
        ifc_config_load_platforms_csv(&config, options.platforms_csv, config_error, sizeof(config_error)) != 0) {
        fprintf(stderr, "error: failed to load platforms CSV %s: %s\n", options.platforms_csv, config_error);
        return 1;
    }
    if (options.system_csv != NULL &&
        ifc_config_load_system_csv(&config, options.system_csv, config_error, sizeof(config_error)) != 0) {
        fprintf(stderr, "error: failed to load system CSV %s: %s\n", options.system_csv, config_error);
        return 1;
    }
    if (options.reference_csv != NULL &&
        ifc_config_load_reference_csv(&config, options.reference_csv, config_error, sizeof(config_error)) != 0) {
        fprintf(stderr, "error: failed to load reference CSV %s: %s\n", options.reference_csv, config_error);
        return 1;
    }

    IfcSimulationRow rows[IFC_ROW_COUNT];
    IfcSummary summary = ifc_simulate_config(&config, rows);
    if (ifc_write_outputs_config(options.output_dir, &config, rows, &summary) != 0) {
        fprintf(stderr, "error: failed to write outputs to %s\n", options.output_dir);
        return 1;
    }
    if (ifc_write_analysis_outputs_config(options.output_dir, &config, rows, &summary) != 0) {
        fprintf(stderr, "error: failed to write analysis outputs to %s\n", options.output_dir);
        return 1;
    }
    if (ifc_write_plots_config(options.output_dir, &config, rows, &summary) != 0) {
        fprintf(stderr, "error: failed to write plots to %s\n", options.output_dir);
        return 1;
    }

    printf("passed: Cambricon-LLM Figure 9 C reproduction\n");
    printf("config_models: %s\n", options.models_csv == NULL ? "default" : options.models_csv);
    printf("config_platforms: %s\n", options.platforms_csv == NULL ? "default" : options.platforms_csv);
    printf("config_system: %s\n", options.system_csv == NULL ? "default" : options.system_csv);
    printf("metrics_csv: %s/figure9_reproduction.csv\n", options.output_dir);
    printf("summary_json: %s/summary.json\n", options.output_dir);
    printf("report_md: %s/report.md\n", options.output_dir);
    printf("request_trace_csv: %s/request_trace.csv\n", options.output_dir);
    printf("controller_timing_csv: %s/controller_timing_summary.csv\n", options.output_dir);
    printf("npu_timing_csv: %s/npu_timing.csv\n", options.output_dir);
    printf("latency_breakdown_csv: %s/latency_breakdown.csv\n", options.output_dir);
    printf("controller_schedule_csv: %s/controller_schedule.csv\n", options.output_dir);
    printf("cycle_controller_trace_csv: %s/cycle_controller_trace.csv\n", options.output_dir);
    printf("cycle_controller_stats_csv: %s/cycle_controller_stats.csv\n", options.output_dir);
    printf("ssdsim_ifc_trace_csv: %s/ssdsim_ifc_trace.csv\n", options.output_dir);
    printf("ssdsim_ifc_stats_csv: %s/ssdsim_ifc_stats.csv\n", options.output_dir);
    printf("ssdsim_ifc_event_trace_csv: %s/ssdsim_ifc_event_trace.csv\n", options.output_dir);
    printf("ssdsim_ifc_event_stats_csv: %s/ssdsim_ifc_event_stats.csv\n", options.output_dir);
    printf("ablation_summary_csv: %s/ablation_summary.csv\n", options.output_dir);
    printf("figure12_read_slice_csv: %s/figure12_read_slice_ablation.csv\n", options.output_dir);
    printf("figure14_tiling_csv: %s/figure14_tiling_ablation.csv\n", options.output_dir);
    printf("platform_summary_csv: %s/platform_summary.csv\n", options.output_dir);
    printf("model_summary_csv: %s/model_summary.csv\n", options.output_dir);
    printf("tile_profile_csv: %s/tile_profile.csv\n", options.output_dir);
    printf("system_profile_csv: %s/system_profile.csv\n", options.output_dir);
    printf("context_inference_csv: %s/context_length_inference.csv\n", options.output_dir);
    printf("reproduction_checks_csv: %s/reproduction_checks.csv\n", options.output_dir);
    printf("raw_plot_dir: %s/figures (local helper output, ignored for release)\n", options.output_dir);
    printf("rows: %d\n", summary.row_count);
    printf("mean_abs_relative_error_pct: %.3f\n", summary.mean_abs_relative_error_pct);
    printf("max_abs_relative_error_pct: %.3f\n", summary.max_abs_relative_error_pct);
    return 0;
}
