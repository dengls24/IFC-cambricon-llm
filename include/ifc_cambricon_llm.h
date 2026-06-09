#ifndef IFC_CAMBRICON_LLM_H
#define IFC_CAMBRICON_LLM_H

#include <stddef.h>

#define IFC_MODEL_COUNT 7
#define IFC_PLATFORM_COUNT 3
#define IFC_ROW_COUNT (IFC_MODEL_COUNT * IFC_PLATFORM_COUNT)
#define IFC_READ_SLICES_PER_REQUEST 4
#define IFC_SAMPLE_TILES 16
#define IFC_NAME_LEN 64
#define IFC_LABEL_LEN 96

typedef enum {
    IFC_OP_READ = 1,
    IFC_OP_WRITE = 2,
    IFC_OP_READ_COMPUTE = 3,
    IFC_OP_READ_SLICE = 4
} IfcCommandOpcode;

typedef struct {
    const char *name;
    const char *label;
    double parameters_billion;
    int layers;
    int hidden_size;
    int attention_heads;
    int cache_heads;
    int head_dim;
} IfcModelProfile;

typedef struct {
    const char *name;
    const char *label;
    int channels;
    int chips_per_channel;
    int dies_per_chip;
    int planes_per_die;
    int compute_cores_per_die;
    int page_bytes;
    double array_read_us;
    double program_us;
    double onfi_rate_MTps;
    int onfi_bus_width_bits;
    double ifc_frequency_hz;
    double ifc_ops_per_core_cycle;
    double channel_bandwidth_Bps;
    double pipeline_efficiency;
    double footprint_penalty;
    double footprint_penalty_power;
    double unsliced_blocking_factor;
    double no_tiling_slowdown;
} IfcPlatformProfile;

typedef struct {
    int compute_cores_per_channel;
    double tile_height;
    double tile_width;
    double tile_payload_bytes;
    double read_compute_request_s;
    double ifc_compute_time_s;
    double read_compute_channel_rate;
    double sliced_read_request_s;
    double alpha_read_compute;
} IfcTileModel;

typedef struct {
    long long last_cycle;
    long long read_compute_commands;
    long long read_slice_commands;
    long long total_commands;
    int command_multiplier;
    double cycle_ns;
    double raw_weight_stage_ms;
    double calibrated_weight_stage_ms;
    double command_address_cycles;
    double read_compute_vector_cycles;
    double read_slice_data_cycles;
    double array_read_cycles;
    double ifc_compute_cycles;
} IfcCycleWeightStats;

typedef struct {
    const char *name;
    const char *label;
    double npu_frequency_hz;
    double npu_ops_per_cycle;
    double npu_peak_ops_per_s;
    double dram_bandwidth_Bps;
} IfcSystemProfile;

typedef struct {
    IfcModelProfile models[IFC_MODEL_COUNT];
    IfcPlatformProfile platforms[IFC_PLATFORM_COUNT];
    IfcSystemProfile system;
    double reference_tokens_per_s[IFC_MODEL_COUNT][IFC_PLATFORM_COUNT];
    int context_tokens;
    char model_names[IFC_MODEL_COUNT][IFC_NAME_LEN];
    char model_labels[IFC_MODEL_COUNT][IFC_LABEL_LEN];
    char platform_names[IFC_PLATFORM_COUNT][IFC_NAME_LEN];
    char platform_labels[IFC_PLATFORM_COUNT][IFC_LABEL_LEN];
    char system_name[IFC_NAME_LEN];
    char system_label[IFC_LABEL_LEN];
} IfcConfig;

typedef struct {
    const char *model;
    const char *model_label;
    const char *platform;
    const char *platform_label;
    int context_tokens;
    double reference_tokens_per_s;
    double simulated_tokens_per_s;
    double relative_error_pct;
    double weight_bytes;
    double attention_cache_bytes;
    double attention_ops;
    double tpot_ms;
    double weight_stage_ms;
    double attention_cache_ms;
    double attention_compute_ms;
    double tile_height;
    double tile_width;
    double alpha_read_compute;
    double effective_pipeline_efficiency;
    double read_compute_requests;
    double npu_read_requests;
    double npu_read_slices;
    double controller_commands;
    long long cycle_weight_last_cycle;
    long long cycle_read_compute_commands;
    long long cycle_read_slice_commands;
    long long cycle_total_commands;
    int cycle_command_multiplier;
    double cycle_ns;
    double cycle_weight_raw_ms;
    double cycle_weight_stage_ms;
    double cycle_command_address_cycles;
    double cycle_read_compute_vector_cycles;
    double cycle_read_slice_data_cycles;
    double cycle_array_read_cycles;
    double cycle_ifc_compute_cycles;
    double read_compute_channel_rate_pct;
    double ifc_read_compute_path_ms;
    double npu_weight_read_path_ms;
    double controller_weight_stage_ms;
    double controller_balance_delta_pct;
    double no_read_slicing_tokens_per_s;
    double no_tiling_tokens_per_s;
    double speedup_vs_no_read_slicing;
    double speedup_vs_no_tiling;
} IfcSimulationRow;

typedef struct {
    int row_count;
    double mean_abs_relative_error_pct;
    double max_abs_relative_error_pct;
    double mean_relative_error_pct;
    const char *worst_case_model;
    const char *worst_case_platform;
} IfcSummary;

extern const IfcModelProfile IFC_MODELS[IFC_MODEL_COUNT];
extern const IfcPlatformProfile IFC_PLATFORMS[IFC_PLATFORM_COUNT];
extern const IfcSystemProfile IFC_DEFAULT_SYSTEM;
extern const double IFC_FIG9_REFERENCE[IFC_MODEL_COUNT][IFC_PLATFORM_COUNT];

const char *ifc_opcode_name(IfcCommandOpcode opcode);
double ifc_platform_channel_bandwidth_Bps(const IfcPlatformProfile *platform);
double ifc_system_npu_peak_ops_per_s(const IfcSystemProfile *system);
void ifc_config_init_default(IfcConfig *config);
int ifc_config_load_models_csv(IfcConfig *config, const char *path, char *error, size_t error_size);
int ifc_config_load_platforms_csv(IfcConfig *config, const char *path, char *error, size_t error_size);
int ifc_config_load_system_csv(IfcConfig *config, const char *path, char *error, size_t error_size);
int ifc_config_load_reference_csv(IfcConfig *config, const char *path, char *error, size_t error_size);
IfcTileModel ifc_derive_tile_model(const IfcPlatformProfile *platform);
IfcSimulationRow ifc_simulate_one(
    const IfcModelProfile *model,
    const IfcPlatformProfile *platform,
    double reference_tokens_per_s,
    int context_tokens);
IfcSimulationRow ifc_simulate_one_with_system(
    const IfcModelProfile *model,
    const IfcPlatformProfile *platform,
    const IfcSystemProfile *system,
    double reference_tokens_per_s,
    int context_tokens);
IfcSummary ifc_simulate_reproduction(IfcSimulationRow rows[IFC_ROW_COUNT], int context_tokens);
IfcSummary ifc_simulate_config(const IfcConfig *config, IfcSimulationRow rows[IFC_ROW_COUNT]);
int ifc_write_sample_controller_schedule(const char *path);
int ifc_write_sample_controller_schedule_for_platform(const char *path, const IfcPlatformProfile *platform);
int ifc_write_cycle_controller_trace_for_platform(
    const char *trace_path,
    const char *stats_path,
    const IfcPlatformProfile *platform);
int ifc_estimate_cycle_weight_stage(
    const IfcPlatformProfile *platform,
    double read_compute_requests,
    double read_slice_commands,
    double effective_efficiency,
    IfcCycleWeightStats *stats);
int ifc_write_ssdsim_ifc_trace_for_platform(
    const char *trace_path,
    const char *stats_path,
    const IfcPlatformProfile *platform);
int ifc_write_ssdsim_ifc_event_trace_for_platform(
    const char *trace_path,
    const char *stats_path,
    const IfcPlatformProfile *platform);
int ifc_write_outputs(const char *output_dir, const IfcSimulationRow rows[IFC_ROW_COUNT], const IfcSummary *summary);
int ifc_write_outputs_config(const char *output_dir, const IfcConfig *config, const IfcSimulationRow rows[IFC_ROW_COUNT], const IfcSummary *summary);
int ifc_write_analysis_outputs(const char *output_dir, const IfcSimulationRow rows[IFC_ROW_COUNT], const IfcSummary *summary);
int ifc_write_analysis_outputs_config(const char *output_dir, const IfcConfig *config, const IfcSimulationRow rows[IFC_ROW_COUNT], const IfcSummary *summary);
int ifc_write_plots(const char *output_dir, const IfcSimulationRow rows[IFC_ROW_COUNT], const IfcSummary *summary);
int ifc_write_plots_config(const char *output_dir, const IfcConfig *config, const IfcSimulationRow rows[IFC_ROW_COUNT], const IfcSummary *summary);

#endif
