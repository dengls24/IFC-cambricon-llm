#ifndef IFC_CAMBRICON_LLM_H
#define IFC_CAMBRICON_LLM_H

#include <stddef.h>

#define IFC_MODEL_COUNT 7
#define IFC_PLATFORM_COUNT 3
#define IFC_ROW_COUNT (IFC_MODEL_COUNT * IFC_PLATFORM_COUNT)
#define IFC_READ_SLICES_PER_REQUEST 4
#define IFC_SAMPLE_TILES 16

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
    double read_compute_channel_rate;
    double sliced_read_request_s;
    double alpha_read_compute;
} IfcTileModel;

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
extern const double IFC_FIG9_REFERENCE[IFC_MODEL_COUNT][IFC_PLATFORM_COUNT];

const char *ifc_opcode_name(IfcCommandOpcode opcode);
IfcTileModel ifc_derive_tile_model(const IfcPlatformProfile *platform);
IfcSimulationRow ifc_simulate_one(
    const IfcModelProfile *model,
    const IfcPlatformProfile *platform,
    double reference_tokens_per_s,
    int context_tokens);
IfcSummary ifc_simulate_reproduction(IfcSimulationRow rows[IFC_ROW_COUNT], int context_tokens);
int ifc_write_outputs(const char *output_dir, const IfcSimulationRow rows[IFC_ROW_COUNT], const IfcSummary *summary);

#endif
