#include "ifc_cambricon_llm.h"

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
    {"cam_llm_s", "Cambricon-LLM-S", 8, 2, 2, 2, 1, 16384, 30.0, 750.0, 1000.0, 8, 1000000000.0, 64.0, 1000000000.0, 0.512, 0.10, 0.75, 0.85, 1.35},
    {"cam_llm_m", "Cambricon-LLM-M", 16, 4, 2, 2, 1, 16384, 30.0, 750.0, 1000.0, 8, 1000000000.0, 64.0, 1000000000.0, 0.344, 0.0, 0.5, 0.85, 1.35},
    {"cam_llm_l", "Cambricon-LLM-L", 32, 8, 2, 2, 1, 16384, 30.0, 750.0, 1000.0, 8, 1000000000.0, 64.0, 1000000000.0, 0.320, 0.50, 0.5, 0.85, 1.35},
};

const IfcSystemProfile IFC_DEFAULT_SYSTEM = {
    "paper_npu",
    "16x16 1GHz INT8 NPU",
    1000000000.0,
    2000.0,
    2000000000000.0,
    40000000000.0,
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

const char *ifc_opcode_name(IfcCommandOpcode opcode) {
    switch (opcode) {
    case IFC_OP_READ:
        return "READ";
    case IFC_OP_WRITE:
        return "WRITE";
    case IFC_OP_READ_COMPUTE:
        return "READ_COMPUTE";
    case IFC_OP_READ_SLICE:
        return "READ_SLICE";
    default:
        return "UNKNOWN";
    }
}
