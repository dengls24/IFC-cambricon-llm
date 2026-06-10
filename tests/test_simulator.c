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

static void require_cycle_trace_consistent(const char *path) {
    FILE *file = fopen(path, "r");
    char line[1024];
    int read_compute_seen = 0;
    int read_slice_seen = 0;
    require_true(file != NULL, "open cycle trace");
    require_true(fgets(line, sizeof(line), file) != NULL, "cycle trace header");
    while (fgets(line, sizeof(line), file) != NULL) {
        int command_id;
        char opcode[32];
        int logical_id;
        int slice_id;
        int channel;
        int chip;
        int die;
        int plane;
        long long arrival_cycle;
        long long channel_start_cycle;
        long long channel_end_cycle;
        long long array_start_cycle;
        long long array_end_cycle;
        long long complete_cycle;
        long long channel_cycles;
        long long array_cycles;
        double complete_ns;
        int parsed = sscanf(
            line,
            "%d,%31[^,],%d,%d,%d,%d,%d,%d,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lf",
            &command_id,
            opcode,
            &logical_id,
            &slice_id,
            &channel,
            &chip,
            &die,
            &plane,
            &arrival_cycle,
            &channel_start_cycle,
            &channel_end_cycle,
            &array_start_cycle,
            &array_end_cycle,
            &complete_cycle,
            &channel_cycles,
            &array_cycles,
            &complete_ns);
        require_true(parsed == 17, "parse cycle trace row");
        require_true(command_id >= 0, "cycle trace command id");
        require_true(channel >= 0, "cycle trace channel");
        require_true(chip >= 0 && die >= 0 && plane >= 0, "cycle trace placement");
        require_true(arrival_cycle <= channel_start_cycle, "cycle trace arrival ordering");
        require_true(channel_end_cycle - channel_start_cycle == channel_cycles, "cycle trace channel cycles");
        require_true(complete_cycle >= channel_end_cycle, "cycle trace completion ordering");
        require_true(complete_ns > 0.0, "cycle trace completion time");
        if (strcmp(opcode, "READ_COMPUTE") == 0) {
            read_compute_seen = 1;
            require_true(array_start_cycle >= channel_end_cycle, "read-compute array ordering");
            require_true(array_end_cycle - array_start_cycle == array_cycles, "read-compute array cycles");
            require_true(complete_cycle == array_end_cycle, "read-compute completion");
        } else if (strcmp(opcode, "READ_SLICE") == 0) {
            read_slice_seen = 1;
            require_true(slice_id >= 0, "read-slice slice id");
            require_true(array_start_cycle == -1 && array_end_cycle == -1, "read-slice array bypass");
            require_true(array_cycles == 0, "read-slice zero array cycles");
            require_true(complete_cycle == channel_end_cycle, "read-slice completion");
        } else {
            require_true(0, "unexpected cycle trace opcode");
        }
    }
    fclose(file);
    require_true(read_compute_seen, "cycle trace READ_COMPUTE commands");
    require_true(read_slice_seen, "cycle trace READ_SLICE commands");
}

static void require_ssdsim_ifc_trace_consistent(const char *path) {
    FILE *file = fopen(path, "r");
    char line[1024];
    int read_compute_ca_seen = 0;
    int read_compute_vector_seen = 0;
    int read_compute_array_seen = 0;
    int read_compute_compute_seen = 0;
    int read_slice_ca_seen = 0;
    int read_slice_data_seen = 0;
    require_true(file != NULL, "open SSDsim IFC trace");
    require_true(fgets(line, sizeof(line), file) != NULL, "SSDsim IFC trace header");
    while (fgets(line, sizeof(line), file) != NULL) {
        int command_id;
        char opcode[32];
        int logical_id;
        int slice_id;
        int channel;
        int chip;
        int die;
        int plane;
        char stage[48];
        char subrequest_state[48];
        char channel_state[48];
        char chip_state[48];
        char plane_state[48];
        long long start_cycle;
        long long end_cycle;
        long long duration_cycles;
        int parsed = sscanf(
            line,
            "%d,%31[^,],%d,%d,%d,%d,%d,%d,%47[^,],%47[^,],%47[^,],%47[^,],%47[^,],%lld,%lld,%lld",
            &command_id,
            opcode,
            &logical_id,
            &slice_id,
            &channel,
            &chip,
            &die,
            &plane,
            stage,
            subrequest_state,
            channel_state,
            chip_state,
            plane_state,
            &start_cycle,
            &end_cycle,
            &duration_cycles);
        require_true(parsed == 16, "parse SSDsim IFC trace row");
        require_true(command_id >= 0, "SSDsim IFC command id");
        require_true(channel >= 0 && chip >= 0 && die >= 0 && plane >= 0, "SSDsim IFC placement");
        require_true(end_cycle - start_cycle == duration_cycles, "SSDsim IFC duration");
        require_true(duration_cycles > 0, "SSDsim IFC positive duration");
        require_true(subrequest_state[0] != '\0', "SSDsim IFC subrequest state");
        require_true(channel_state[0] != '\0', "SSDsim IFC channel state");
        require_true(chip_state[0] != '\0', "SSDsim IFC chip state");
        require_true(plane_state[0] != '\0', "SSDsim IFC plane state");
        if (strcmp(opcode, "READ_COMPUTE") == 0) {
            require_true(slice_id == -1, "SSDsim IFC read-compute slice id");
            if (strcmp(stage, "SSDSIM_CA_TRANSFER") == 0) {
                read_compute_ca_seen = 1;
                require_true(strcmp(channel_state, "CHANNEL_C_A_TRANSFER") == 0, "read-compute C/A channel state");
                require_true(strcmp(chip_state, "CHIP_C_A_TRANSFER") == 0, "read-compute C/A chip state");
            } else if (strcmp(stage, "IFC_VECTOR_TRANSFER") == 0) {
                read_compute_vector_seen = 1;
                require_true(strcmp(channel_state, "CHANNEL_DATA_TRANSFER") == 0, "read-compute vector channel state");
            } else if (strcmp(stage, "SSDSIM_ARRAY_READ") == 0) {
                read_compute_array_seen = 1;
                require_true(strcmp(channel_state, "CHANNEL_IDLE") == 0, "read-compute array channel state");
                require_true(strcmp(chip_state, "CHIP_READ_BUSY") == 0, "read-compute array chip state");
            } else if (strcmp(stage, "IFC_COMPUTE") == 0) {
                read_compute_compute_seen = 1;
                require_true(strcmp(chip_state, "CHIP_IFC_COMPUTE") == 0, "read-compute IFC chip state");
            } else {
                require_true(0, "unexpected read-compute SSDsim IFC stage");
            }
        } else if (strcmp(opcode, "READ_SLICE") == 0) {
            require_true(slice_id >= 0, "SSDsim IFC read-slice slice id");
            if (strcmp(stage, "SSDSIM_CA_TRANSFER") == 0) {
                read_slice_ca_seen = 1;
            } else if (strcmp(stage, "SSDSIM_DATA_TRANSFER") == 0) {
                read_slice_data_seen = 1;
                require_true(strcmp(channel_state, "CHANNEL_DATA_TRANSFER") == 0, "read-slice data channel state");
                require_true(strcmp(chip_state, "CHIP_DATA_TRANSFER") == 0, "read-slice data chip state");
            } else {
                require_true(0, "unexpected read-slice SSDsim IFC stage");
            }
        } else {
            require_true(0, "unexpected SSDsim IFC opcode");
        }
    }
    fclose(file);
    require_true(read_compute_ca_seen, "SSDsim IFC READ_COMPUTE C/A stage");
    require_true(read_compute_vector_seen, "SSDsim IFC READ_COMPUTE vector stage");
    require_true(read_compute_array_seen, "SSDsim IFC READ_COMPUTE array stage");
    require_true(read_compute_compute_seen, "SSDsim IFC READ_COMPUTE compute stage");
    require_true(read_slice_ca_seen, "SSDsim IFC READ_SLICE C/A stage");
    require_true(read_slice_data_seen, "SSDsim IFC READ_SLICE data stage");
}

static void require_ssdsim_ifc_event_trace_consistent(const char *path) {
    FILE *file = fopen(path, "r");
    char line[1024];
    long long previous_cycle = -1;
    int issue_events = 0;
    int complete_events = 0;
    int read_compute_compute_issue_seen = 0;
    int read_compute_array_complete_seen = 0;
    int read_slice_data_complete_seen = 0;
    require_true(file != NULL, "open SSDsim IFC event trace");
    require_true(fgets(line, sizeof(line), file) != NULL, "SSDsim IFC event trace header");
    while (fgets(line, sizeof(line), file) != NULL) {
        int event_id;
        long long event_cycle;
        char event_type[32];
        int command_id;
        char opcode[32];
        int logical_id;
        int slice_id;
        char stage[48];
        char subrequest_state[48];
        char channel_state[48];
        char chip_state[48];
        char plane_state[48];
        int channel;
        int chip;
        int die;
        int plane;
        long long stage_start_cycle;
        long long stage_end_cycle;
        long long duration_cycles;
        int active_commands;
        int parsed = sscanf(
            line,
            "%d,%lld,%31[^,],%d,%31[^,],%d,%d,%47[^,],%47[^,],%47[^,],%47[^,],%47[^,],%d,%d,%d,%d,%lld,%lld,%lld,%d",
            &event_id,
            &event_cycle,
            event_type,
            &command_id,
            opcode,
            &logical_id,
            &slice_id,
            stage,
            subrequest_state,
            channel_state,
            chip_state,
            plane_state,
            &channel,
            &chip,
            &die,
            &plane,
            &stage_start_cycle,
            &stage_end_cycle,
            &duration_cycles,
            &active_commands);
        require_true(parsed == 20, "parse SSDsim IFC event row");
        require_true(event_id >= 0, "SSDsim IFC event id");
        require_true(command_id >= 0, "SSDsim IFC event command id");
        require_true(event_cycle >= previous_cycle, "SSDsim IFC event monotonic time");
        require_true(stage_end_cycle - stage_start_cycle == duration_cycles, "SSDsim IFC event duration");
        require_true(duration_cycles > 0, "SSDsim IFC event positive duration");
        require_true(active_commands >= 1, "SSDsim IFC active command count");
        require_true(channel >= 0 && chip >= 0 && die >= 0 && plane >= 0, "SSDsim IFC event placement");
        if (strcmp(event_type, "ISSUE") == 0) {
            ++issue_events;
            require_true(event_cycle == stage_start_cycle, "SSDsim IFC issue cycle");
        } else if (strcmp(event_type, "COMPLETE") == 0) {
            ++complete_events;
            require_true(event_cycle == stage_end_cycle, "SSDsim IFC complete cycle");
        } else {
            require_true(0, "unexpected SSDsim IFC event type");
        }
        if (strcmp(opcode, "READ_COMPUTE") == 0 && strcmp(stage, "IFC_COMPUTE") == 0 &&
            strcmp(event_type, "ISSUE") == 0) {
            read_compute_compute_issue_seen = 1;
            require_true(strcmp(chip_state, "CHIP_IFC_COMPUTE") == 0, "event IFC compute chip state");
        }
        if (strcmp(opcode, "READ_COMPUTE") == 0 && strcmp(stage, "SSDSIM_ARRAY_READ") == 0 &&
            strcmp(event_type, "COMPLETE") == 0) {
            read_compute_array_complete_seen = 1;
            require_true(strcmp(subrequest_state, "SR_R_READ") == 0, "event array subrequest state");
        }
        if (strcmp(opcode, "READ_SLICE") == 0 && strcmp(stage, "SSDSIM_DATA_TRANSFER") == 0 &&
            strcmp(event_type, "COMPLETE") == 0) {
            read_slice_data_complete_seen = 1;
            require_true(slice_id >= 0, "event read-slice slice id");
        }
        previous_cycle = event_cycle;
    }
    fclose(file);
    require_true(issue_events > 0, "SSDsim IFC issue events");
    require_true(complete_events > 0, "SSDsim IFC complete events");
    require_true(issue_events == complete_events, "SSDsim IFC issue/complete balance");
    require_true(read_compute_compute_issue_seen, "SSDsim IFC event READ_COMPUTE compute issue");
    require_true(read_compute_array_complete_seen, "SSDsim IFC event READ_COMPUTE array complete");
    require_true(read_slice_data_complete_seen, "SSDsim IFC event READ_SLICE data complete");
}

static void require_compare_passes(const char *path) {
    FILE *file = fopen(path, "r");
    char line[512];
    int pass_count = 0;
    require_true(file != NULL, "open hardware compare");
    require_true(fgets(line, sizeof(line), file) != NULL, "hardware compare header");
    while (fgets(line, sizeof(line), file) != NULL) {
        char metric[64];
        char c_backend[64];
        char hw_cycle[64];
        char delta[64];
        char status[32];
        int parsed = sscanf(line, "%63[^,],%63[^,],%63[^,],%63[^,],%31[^\n\r]", metric, c_backend, hw_cycle, delta, status);
        require_true(parsed == 5, "parse hardware compare row");
        require_true(strcmp(status, "PASS") == 0, "hardware compare pass");
        ++pass_count;
    }
    fclose(file);
    require_true(pass_count >= 3, "hardware compare row count");
}

static void require_context_inference_consistent(const char *path) {
    FILE *file = fopen(path, "r");
    char line[512];
    int row_count = 0;
    int default_seen = 0;
    int best_rmse_seen = 0;
    int best_max_seen = 0;
    require_true(file != NULL, "open context inference");
    require_true(fgets(line, sizeof(line), file) != NULL, "context inference header");
    while (fgets(line, sizeof(line), file) != NULL) {
        int context_tokens;
        double mean_abs_error;
        double rmse_error;
        double max_abs_error;
        double mean_error;
        int guardrail_pass;
        int best_mae;
        int best_rmse;
        int best_max;
        int default_context;
        int guardrail_min;
        int guardrail_max;
        char interpretation[64];
        int parsed = sscanf(
            line,
            "%d,%lf,%lf,%lf,%lf,%d,%d,%d,%d,%d,%d,%d,%63[^\n\r]",
            &context_tokens,
            &mean_abs_error,
            &rmse_error,
            &max_abs_error,
            &mean_error,
            &guardrail_pass,
            &best_mae,
            &best_rmse,
            &best_max,
            &default_context,
            &guardrail_min,
            &guardrail_max,
            interpretation);
        require_true(parsed == 13, "parse context inference row");
        require_true(context_tokens >= 1 && context_tokens <= 4096, "context inference range");
        require_true(mean_abs_error > 0.0 && rmse_error > 0.0 && max_abs_error > 0.0, "context inference metrics");
        require_true(guardrail_min <= 1000 && guardrail_max >= 1000, "context guardrail includes 1K");
        if (default_context) {
            default_seen = 1;
            require_true(context_tokens == 1000, "default context marker");
            require_true(guardrail_pass, "default context guardrail pass");
            require_true(mean_abs_error <= 9.0, "default context mean error");
            require_true(max_abs_error <= 15.0, "default context max error");
        }
        if (best_rmse) {
            best_rmse_seen = 1;
            require_true(context_tokens >= 900 && context_tokens <= 1100, "best RMSE context near 1K");
        }
        if (best_max) {
            best_max_seen = 1;
            require_true(context_tokens >= 900 && context_tokens <= 1100, "best max-error context near 1K");
        }
        ++row_count;
    }
    fclose(file);
    require_true(row_count == 4096, "context inference row count");
    require_true(default_seen, "context inference default marker");
    require_true(best_rmse_seen, "context inference RMSE marker");
    require_true(best_max_seen, "context inference max-error marker");
}

static int expected_operator_trace_events(const IfcConfig *config) {
    int total = 0;
    for (int model_id = 0; model_id < IFC_MODEL_COUNT; ++model_id) {
        total += config->models[model_id].layers * IFC_PLATFORM_COUNT * IFC_TRACE_OPS_PER_LAYER;
    }
    return total;
}

static void require_operator_rows_consistent(
    const IfcConfig *config,
    const IfcSimulationRow rows[IFC_ROW_COUNT]) {
    for (int i = 0; i < IFC_ROW_COUNT; ++i) {
        int model_id = i / IFC_PLATFORM_COUNT;
        int layers = config->models[model_id].layers;
        require_true(rows[i].operator_trace_ops == layers * IFC_TRACE_OPS_PER_LAYER, "operator trace op count");
        require_true(rows[i].operator_trace_ifc_ops == layers * 4, "operator trace IFC op count");
        require_true(rows[i].operator_trace_dram_ops == layers * 2, "operator trace DRAM op count");
        require_true(rows[i].operator_trace_npu_ops == layers * 7, "operator trace NPU op count");
        require_close(rows[i].operator_trace_total_ms, rows[i].tpot_ms, 1e-6, "operator trace TPOT source");
        require_close(rows[i].operator_trace_total_ms, rows[i].legacy_tpot_ms, 1e-6, "operator trace legacy TPOT parity");
        require_close(rows[i].operator_trace_ifc_ms, rows[i].weight_stage_ms, 1e-6, "operator trace IFC latency total");
        require_close(rows[i].operator_trace_dram_ms, rows[i].attention_cache_ms, 1e-6, "operator trace DRAM latency total");
        require_close(rows[i].operator_trace_npu_ms, rows[i].attention_compute_ms, 1e-6, "operator trace NPU latency total");
        require_close(rows[i].operator_trace_weight_bytes, rows[i].weight_bytes, 1e-3, "operator trace weight bytes");
        require_close(rows[i].operator_trace_dram_bytes, rows[i].attention_cache_bytes, 1e-3, "operator trace DRAM bytes");
        require_close(rows[i].operator_trace_npu_ops_value, rows[i].attention_ops, 1e-3, "operator trace NPU ops");
        require_true(rows[i].operator_trace_dram_bursts > 0, "operator trace DRAM bursts");
        require_close(rows[i].operator_trace_delta_pct, 0.0, 1e-9, "operator trace TPOT delta");
    }
}

static void require_operator_trace_consistent(
    const char *path,
    const IfcSimulationRow rows[IFC_ROW_COUNT],
    int expected_events,
    int expected_rows) {
    FILE *file = fopen(path, "r");
    char line[1024];
    int row_counts[IFC_ROW_COUNT];
    double row_last_end_ms[IFC_ROW_COUNT];
    int events = 0;
    int ifc_seen = 0;
    int dram_seen = 0;
    int npu_seen = 0;
    memset(row_counts, 0, sizeof(row_counts));
    memset(row_last_end_ms, 0, sizeof(row_last_end_ms));
    require_true(file != NULL, "open operator trace");
    require_true(fgets(line, sizeof(line), file) != NULL, "operator trace header");
    while (fgets(line, sizeof(line), file) != NULL) {
        int row_id;
        char model[64];
        char platform[64];
        int layer_id;
        int op_index;
        char op_type[64];
        char engine[32];
        char mapped_stage[64];
        double engine_ready_ms;
        double start_ms;
        double end_ms;
        double service_ms;
        double weight_bytes;
        double dram_bytes;
        double npu_ops;
        double cycle_ns;
        long long service_cycles;
        long long dram_bursts;
        int parsed = sscanf(
            line,
            "%d,%63[^,],%63[^,],%d,%d,%63[^,],%31[^,],%63[^,],%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lld,%lld",
            &row_id,
            model,
            platform,
            &layer_id,
            &op_index,
            op_type,
            engine,
            mapped_stage,
            &engine_ready_ms,
            &start_ms,
            &end_ms,
            &service_ms,
            &weight_bytes,
            &dram_bytes,
            &npu_ops,
            &cycle_ns,
            &service_cycles,
            &dram_bursts);
        require_true(parsed == 18, "parse operator trace row");
        require_true(row_id >= 0 && row_id < expected_rows, "operator trace row id");
        require_true(layer_id >= 0, "operator trace layer id");
        require_true(op_index >= 0 && op_index < IFC_TRACE_OPS_PER_LAYER, "operator trace op index");
        require_true(model[0] != '\0' && platform[0] != '\0' && op_type[0] != '\0', "operator trace labels");
        require_true(mapped_stage[0] != '\0', "operator trace mapped stage");
        require_true(start_ms >= engine_ready_ms, "operator trace engine ready ordering");
        require_true(end_ms >= start_ms, "operator trace end ordering");
        require_close(end_ms - start_ms, service_ms, 1e-6, "operator trace service interval");
        require_true(service_ms > 0.0, "operator trace positive service");
        require_true(cycle_ns > 0.0, "operator trace cycle");
        require_true(service_cycles > 0, "operator trace service cycles");
        if (strcmp(engine, "IFC") == 0) {
            ifc_seen = 1;
            require_true(weight_bytes > 0.0, "operator trace IFC weight bytes");
            require_true(dram_bytes == 0.0 && npu_ops == 0.0, "operator trace IFC work isolation");
        } else if (strcmp(engine, "DRAM") == 0) {
            dram_seen = 1;
            require_true(dram_bytes > 0.0, "operator trace DRAM bytes");
            require_true(dram_bursts > 0, "operator trace DRAM bursts");
            require_true(weight_bytes == 0.0 && npu_ops == 0.0, "operator trace DRAM work isolation");
        } else if (strcmp(engine, "NPU") == 0) {
            npu_seen = 1;
            require_true(npu_ops > 0.0, "operator trace NPU ops");
            require_true(weight_bytes == 0.0 && dram_bytes == 0.0, "operator trace NPU work isolation");
        } else {
            require_true(0, "unexpected operator trace engine");
        }
        if (end_ms > row_last_end_ms[row_id]) {
            row_last_end_ms[row_id] = end_ms;
        }
        ++row_counts[row_id];
        ++events;
    }
    fclose(file);
    require_true(events == expected_events, "operator trace event count");
    for (int row_id = 0; row_id < expected_rows; ++row_id) {
        require_true(row_counts[row_id] > 0, "operator trace per-row coverage");
        require_close(row_last_end_ms[row_id], rows[row_id].operator_trace_total_ms, 1e-6, "operator trace final time");
    }
    require_true(ifc_seen && dram_seen && npu_seen, "operator trace engine coverage");
}

int main(void) {
    IfcConfig default_config;
    IfcTileModel tile = ifc_derive_tile_model(&IFC_PLATFORMS[0]);
    ifc_config_init_default(&default_config);
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
    require_operator_rows_consistent(&default_config, rows);

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
        require_true(rows[i].cycle_weight_last_cycle > 0, "microcycle-derived weight stage cycle");
        require_true(rows[i].cycle_total_commands > rows[i].controller_commands, "full-row physical command expansion");
        require_true(rows[i].cycle_weight_raw_ms > 0.0, "microcycle-derived raw weight time");
        require_close(rows[i].weight_stage_ms, rows[i].cycle_weight_stage_ms, 1e-6, "microcycle-derived weight stage source");
        require_true(rows[i].cycle_issue_width == 8, "microcycle issue width");
        require_true(rows[i].cycle_issue_fifo_depth == 8, "microcycle FIFO depth");
        require_close(rows[i].cycle_module_clock_ns, 2.5, 1e-9, "microcycle module clock");
        require_true(
            rows[i].cycle_stage_issue_events ==
                rows[i].cycle_read_compute_commands * 4LL + rows[i].cycle_read_slice_commands * 2LL,
            "microcycle stage issue event count");
        require_true(rows[i].cycle_dispatch_rounds > 0, "microcycle dispatch rounds");
        require_true(rows[i].speedup_vs_no_read_slicing > 1.0, "read slicing positive speedup");
        require_true(rows[i].speedup_vs_no_tiling > 1.0, "tiling positive speedup");
        if (strcmp(rows[i].platform, IFC_PLATFORMS[0].name) == 0) {
            require_true(rows[i].speedup_vs_no_read_slicing > 1.60, "Cambricon-LLM-S read slicing lower speedup bound");
            require_true(rows[i].speedup_vs_no_read_slicing < 1.80, "Cambricon-LLM-S read slicing upper speedup bound");
            require_true(rows[i].speedup_vs_no_tiling > 1.30, "Cambricon-LLM-S tiling lower speedup bound");
            require_true(rows[i].speedup_vs_no_tiling < 1.40, "Cambricon-LLM-S tiling upper speedup bound");
        }
    }

    const char *artifact_dir = "/tmp/ifc_cambricon_llm_test_outputs";
    require_true(ifc_write_outputs(artifact_dir, rows, &summary) == 0, "write primary outputs");
    require_true(ifc_write_analysis_outputs(artifact_dir, rows, &summary) == 0, "write analysis outputs");
    require_true(ifc_write_plots(artifact_dir, rows, &summary) == 0, "write plot outputs");
    require_nonempty_file("/tmp/ifc_cambricon_llm_test_outputs/figure9_reproduction.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_test_outputs/platform_summary.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_test_outputs/latency_breakdown.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_test_outputs/cycle_weight_timing.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_test_outputs/cycle_controller_trace.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_test_outputs/cycle_controller_stats.csv");
    require_cycle_trace_consistent("/tmp/ifc_cambricon_llm_test_outputs/cycle_controller_trace.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_test_outputs/ssdsim_ifc_trace.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_test_outputs/ssdsim_ifc_stats.csv");
    require_ssdsim_ifc_trace_consistent("/tmp/ifc_cambricon_llm_test_outputs/ssdsim_ifc_trace.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_test_outputs/ssdsim_ifc_event_trace.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_test_outputs/ssdsim_ifc_event_stats.csv");
    require_ssdsim_ifc_event_trace_consistent("/tmp/ifc_cambricon_llm_test_outputs/ssdsim_ifc_event_trace.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_test_outputs/reproduction_checks.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_test_outputs/system_profile.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_test_outputs/context_length_inference.csv");
    require_context_inference_consistent("/tmp/ifc_cambricon_llm_test_outputs/context_length_inference.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_test_outputs/operator_trace.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_test_outputs/operator_trace_summary.csv");
    require_operator_trace_consistent(
        "/tmp/ifc_cambricon_llm_test_outputs/operator_trace.csv",
        rows,
        expected_operator_trace_events(&default_config),
        IFC_ROW_COUNT);
    require_nonempty_file("/tmp/ifc_cambricon_llm_test_outputs/figures/plot_manifest.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_test_outputs/figures/figure9_plot_source.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_test_outputs/figures/controller_timeline_source.csv");

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
    require_operator_rows_consistent(&custom_config, custom_rows);
    require_true(ifc_write_outputs_config("/tmp/ifc_cambricon_llm_custom_outputs", &custom_config, custom_rows, &custom_summary) == 0, "write custom outputs");
    require_true(ifc_write_analysis_outputs_config("/tmp/ifc_cambricon_llm_custom_outputs", &custom_config, custom_rows, &custom_summary) == 0, "write custom analysis outputs");
    require_true(ifc_write_plots_config("/tmp/ifc_cambricon_llm_custom_outputs", &custom_config, custom_rows, &custom_summary) == 0, "write custom plot outputs");
    require_nonempty_file("/tmp/ifc_cambricon_llm_custom_outputs/tile_profile.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_custom_outputs/latency_breakdown.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_custom_outputs/cycle_weight_timing.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_custom_outputs/cycle_controller_trace.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_custom_outputs/cycle_controller_stats.csv");
    require_cycle_trace_consistent("/tmp/ifc_cambricon_llm_custom_outputs/cycle_controller_trace.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_custom_outputs/ssdsim_ifc_trace.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_custom_outputs/ssdsim_ifc_stats.csv");
    require_ssdsim_ifc_trace_consistent("/tmp/ifc_cambricon_llm_custom_outputs/ssdsim_ifc_trace.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_custom_outputs/ssdsim_ifc_event_trace.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_custom_outputs/ssdsim_ifc_event_stats.csv");
    require_ssdsim_ifc_event_trace_consistent("/tmp/ifc_cambricon_llm_custom_outputs/ssdsim_ifc_event_trace.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_custom_outputs/system_profile.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_custom_outputs/context_length_inference.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_custom_outputs/operator_trace.csv");
    require_nonempty_file("/tmp/ifc_cambricon_llm_custom_outputs/operator_trace_summary.csv");
    require_operator_trace_consistent(
        "/tmp/ifc_cambricon_llm_custom_outputs/operator_trace.csv",
        custom_rows,
        expected_operator_trace_events(&custom_config),
        IFC_ROW_COUNT);
    require_nonempty_file("/tmp/ifc_cambricon_llm_custom_outputs/figures/controller_timeline_source.csv");

    require_true(system("make hw-cycle >/tmp/ifc_cambricon_llm_hw_cycle_test.log 2>&1") == 0, "run hardware cycle target");
    require_nonempty_file("results/hw_cycle_trace.csv");
    require_nonempty_file("results/hw_cycle_stats.csv");
    require_nonempty_file("results/hw_cycle_compare.csv");
    require_ssdsim_ifc_event_trace_consistent("results/hw_cycle_trace.csv");
    require_compare_passes("results/hw_cycle_compare.csv");

    printf("passed: C simulator tests\n");
    return 0;
}
