#include "ifc_cambricon_llm.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(char *error, size_t error_size, const char *message) {
    if (error != NULL && error_size > 0) {
        snprintf(error, error_size, "%s", message);
    }
}

static char *trim(char *text) {
    char *end;
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') {
        ++text;
    }
    end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        --end;
    }
    *end = '\0';
    return text;
}

static int split_csv_line(char *line, char *fields[], int max_fields) {
    int count = 0;
    char *scan = line;
    while (count < max_fields) {
        char *comma = strchr(scan, ',');
        if (comma != NULL) {
            *comma = '\0';
        }
        fields[count++] = trim(scan);
        if (comma == NULL) {
            break;
        }
        scan = comma + 1;
    }
    return count;
}

static double parse_double(const char *text) {
    return strtod(text, NULL);
}

static int parse_int(const char *text) {
    return (int)strtol(text, NULL, 10);
}

static void copy_text(char *storage, size_t storage_size, const char **target, const char *source) {
    snprintf(storage, storage_size, "%s", source);
    *target = storage;
}

double ifc_platform_channel_bandwidth_Bps(const IfcPlatformProfile *platform) {
    if (platform->channel_bandwidth_Bps > 0.0) {
        return platform->channel_bandwidth_Bps;
    }
    return platform->onfi_rate_MTps * 1e6 * (double)platform->onfi_bus_width_bits / 8.0;
}

double ifc_system_npu_peak_ops_per_s(const IfcSystemProfile *system) {
    if (system->npu_peak_ops_per_s > 0.0) {
        return system->npu_peak_ops_per_s;
    }
    return system->npu_frequency_hz * system->npu_ops_per_cycle;
}

void ifc_config_init_default(IfcConfig *config) {
    memset(config, 0, sizeof(*config));
    config->context_tokens = 1000;

    for (int i = 0; i < IFC_MODEL_COUNT; ++i) {
        config->models[i] = IFC_MODELS[i];
        copy_text(config->model_names[i], sizeof(config->model_names[i]), &config->models[i].name, IFC_MODELS[i].name);
        copy_text(config->model_labels[i], sizeof(config->model_labels[i]), &config->models[i].label, IFC_MODELS[i].label);
    }
    for (int i = 0; i < IFC_PLATFORM_COUNT; ++i) {
        config->platforms[i] = IFC_PLATFORMS[i];
        copy_text(config->platform_names[i], sizeof(config->platform_names[i]), &config->platforms[i].name, IFC_PLATFORMS[i].name);
        copy_text(config->platform_labels[i], sizeof(config->platform_labels[i]), &config->platforms[i].label, IFC_PLATFORMS[i].label);
    }
    config->system = IFC_DEFAULT_SYSTEM;
    copy_text(config->system_name, sizeof(config->system_name), &config->system.name, IFC_DEFAULT_SYSTEM.name);
    copy_text(config->system_label, sizeof(config->system_label), &config->system.label, IFC_DEFAULT_SYSTEM.label);

    for (int model_id = 0; model_id < IFC_MODEL_COUNT; ++model_id) {
        for (int platform_id = 0; platform_id < IFC_PLATFORM_COUNT; ++platform_id) {
            config->reference_tokens_per_s[model_id][platform_id] = IFC_FIG9_REFERENCE[model_id][platform_id];
        }
    }
}

int ifc_config_load_models_csv(IfcConfig *config, const char *path, char *error, size_t error_size) {
    FILE *file = fopen(path, "r");
    char line[2048];
    int row = 0;
    if (file == NULL) {
        set_error(error, error_size, strerror(errno));
        return -1;
    }
    if (fgets(line, sizeof(line), file) == NULL) {
        fclose(file);
        set_error(error, error_size, "models CSV is empty");
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *fields[8];
        int count;
        if (trim(line)[0] == '\0') {
            continue;
        }
        if (row >= IFC_MODEL_COUNT) {
            fclose(file);
            set_error(error, error_size, "models CSV has too many rows");
            return -1;
        }
        count = split_csv_line(line, fields, 8);
        if (count != 8) {
            fclose(file);
            set_error(error, error_size, "models CSV expects 8 columns");
            return -1;
        }
        copy_text(config->model_names[row], sizeof(config->model_names[row]), &config->models[row].name, fields[0]);
        copy_text(config->model_labels[row], sizeof(config->model_labels[row]), &config->models[row].label, fields[1]);
        config->models[row].parameters_billion = parse_double(fields[2]);
        config->models[row].layers = parse_int(fields[3]);
        config->models[row].hidden_size = parse_int(fields[4]);
        config->models[row].attention_heads = parse_int(fields[5]);
        config->models[row].cache_heads = parse_int(fields[6]);
        config->models[row].head_dim = parse_int(fields[7]);
        ++row;
    }
    fclose(file);
    if (row != IFC_MODEL_COUNT) {
        set_error(error, error_size, "models CSV must contain exactly 7 data rows");
        return -1;
    }
    return 0;
}

int ifc_config_load_platforms_csv(IfcConfig *config, const char *path, char *error, size_t error_size) {
    FILE *file = fopen(path, "r");
    char line[4096];
    int row = 0;
    if (file == NULL) {
        set_error(error, error_size, strerror(errno));
        return -1;
    }
    if (fgets(line, sizeof(line), file) == NULL) {
        fclose(file);
        set_error(error, error_size, "platforms CSV is empty");
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *fields[19];
        int count;
        if (trim(line)[0] == '\0') {
            continue;
        }
        if (row >= IFC_PLATFORM_COUNT) {
            fclose(file);
            set_error(error, error_size, "platforms CSV has too many rows");
            return -1;
        }
        count = split_csv_line(line, fields, 19);
        if (count != 19) {
            fclose(file);
            set_error(error, error_size, "platforms CSV expects 19 columns");
            return -1;
        }
        copy_text(config->platform_names[row], sizeof(config->platform_names[row]), &config->platforms[row].name, fields[0]);
        copy_text(config->platform_labels[row], sizeof(config->platform_labels[row]), &config->platforms[row].label, fields[1]);
        config->platforms[row].channels = parse_int(fields[2]);
        config->platforms[row].chips_per_channel = parse_int(fields[3]);
        config->platforms[row].dies_per_chip = parse_int(fields[4]);
        config->platforms[row].planes_per_die = parse_int(fields[5]);
        config->platforms[row].compute_cores_per_die = parse_int(fields[6]);
        config->platforms[row].page_bytes = parse_int(fields[7]);
        config->platforms[row].array_read_us = parse_double(fields[8]);
        config->platforms[row].program_us = parse_double(fields[9]);
        config->platforms[row].onfi_rate_MTps = parse_double(fields[10]);
        config->platforms[row].onfi_bus_width_bits = parse_int(fields[11]);
        config->platforms[row].ifc_frequency_hz = parse_double(fields[12]) * 1e6;
        config->platforms[row].ifc_ops_per_core_cycle = parse_double(fields[13]);
        config->platforms[row].channel_bandwidth_Bps =
            config->platforms[row].onfi_rate_MTps * 1e6 * (double)config->platforms[row].onfi_bus_width_bits / 8.0;
        config->platforms[row].pipeline_efficiency = parse_double(fields[14]);
        config->platforms[row].footprint_penalty = parse_double(fields[15]);
        config->platforms[row].footprint_penalty_power = parse_double(fields[16]);
        config->platforms[row].unsliced_blocking_factor = parse_double(fields[17]);
        config->platforms[row].no_tiling_slowdown = parse_double(fields[18]);
        ++row;
    }
    fclose(file);
    if (row != IFC_PLATFORM_COUNT) {
        set_error(error, error_size, "platforms CSV must contain exactly 3 data rows");
        return -1;
    }
    return 0;
}

int ifc_config_load_system_csv(IfcConfig *config, const char *path, char *error, size_t error_size) {
    FILE *file = fopen(path, "r");
    char line[1024];
    if (file == NULL) {
        set_error(error, error_size, strerror(errno));
        return -1;
    }
    if (fgets(line, sizeof(line), file) == NULL) {
        fclose(file);
        set_error(error, error_size, "system CSV is empty");
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *fields[2];
        int count;
        if (trim(line)[0] == '\0') {
            continue;
        }
        count = split_csv_line(line, fields, 2);
        if (count != 2) {
            fclose(file);
            set_error(error, error_size, "system CSV expects key,value rows");
            return -1;
        }
        if (strcmp(fields[0], "name") == 0) {
            copy_text(config->system_name, sizeof(config->system_name), &config->system.name, fields[1]);
        } else if (strcmp(fields[0], "label") == 0) {
            copy_text(config->system_label, sizeof(config->system_label), &config->system.label, fields[1]);
        } else if (strcmp(fields[0], "context_tokens") == 0) {
            config->context_tokens = parse_int(fields[1]);
        } else if (strcmp(fields[0], "npu_frequency_MHz") == 0) {
            config->system.npu_frequency_hz = parse_double(fields[1]) * 1e6;
        } else if (strcmp(fields[0], "npu_ops_per_cycle") == 0) {
            config->system.npu_ops_per_cycle = parse_double(fields[1]);
        } else if (strcmp(fields[0], "npu_peak_TOPS") == 0) {
            config->system.npu_peak_ops_per_s = parse_double(fields[1]) * 1e12;
        } else if (strcmp(fields[0], "dram_bandwidth_GBps") == 0) {
            config->system.dram_bandwidth_Bps = parse_double(fields[1]) * 1e9;
        }
    }
    fclose(file);
    if (config->system.npu_peak_ops_per_s <= 0.0) {
        config->system.npu_peak_ops_per_s =
            config->system.npu_frequency_hz * config->system.npu_ops_per_cycle;
    }
    return 0;
}

int ifc_config_load_reference_csv(IfcConfig *config, const char *path, char *error, size_t error_size) {
    FILE *file = fopen(path, "r");
    char line[2048];
    int row = 0;
    if (file == NULL) {
        set_error(error, error_size, strerror(errno));
        return -1;
    }
    if (fgets(line, sizeof(line), file) == NULL) {
        fclose(file);
        set_error(error, error_size, "reference CSV is empty");
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *fields[4];
        int count;
        if (trim(line)[0] == '\0') {
            continue;
        }
        if (row >= IFC_MODEL_COUNT) {
            fclose(file);
            set_error(error, error_size, "reference CSV has too many rows");
            return -1;
        }
        count = split_csv_line(line, fields, 4);
        if (count != 4) {
            fclose(file);
            set_error(error, error_size, "reference CSV expects model plus 3 platform columns");
            return -1;
        }
        for (int platform_id = 0; platform_id < IFC_PLATFORM_COUNT; ++platform_id) {
            config->reference_tokens_per_s[row][platform_id] = parse_double(fields[platform_id + 1]);
        }
        ++row;
    }
    fclose(file);
    if (row != IFC_MODEL_COUNT) {
        set_error(error, error_size, "reference CSV must contain exactly 7 data rows");
        return -1;
    }
    return 0;
}
