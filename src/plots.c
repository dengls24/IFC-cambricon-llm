#include "ifc_cambricon_llm.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static int ensure_dir(const char *path) {
    if (mkdir(path, 0775) == 0) {
        return 0;
    }
    if (errno == EEXIST) {
        return 0;
    }
    return -1;
}

static void join_path(char *buffer, size_t buffer_size, const char *dir, const char *name) {
    size_t len = strlen(dir);
    if (len > 0 && dir[len - 1] == '/') {
        snprintf(buffer, buffer_size, "%s%s", dir, name);
    } else {
        snprintf(buffer, buffer_size, "%s/%s", dir, name);
    }
}

static void svg_header(FILE *file, int width, int height, const char *title) {
    fprintf(file, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" viewBox=\"0 0 %d %d\">\n", width, height, width, height);
    fprintf(file, "<rect width=\"100%%\" height=\"100%%\" fill=\"#ffffff\"/>\n");
    fprintf(file, "<text x=\"%d\" y=\"34\" text-anchor=\"middle\" font-family=\"Arial,Helvetica,sans-serif\" font-size=\"20\" font-weight=\"700\" fill=\"#202124\">%s</text>\n", width / 2, title);
}

static void svg_footer(FILE *file) {
    fprintf(file, "</svg>\n");
}

static double row_max_speed(const IfcSimulationRow rows[IFC_ROW_COUNT]) {
    double max_value = 0.0;
    for (int i = 0; i < IFC_ROW_COUNT; ++i) {
        if (rows[i].reference_tokens_per_s > max_value) {
            max_value = rows[i].reference_tokens_per_s;
        }
        if (rows[i].simulated_tokens_per_s > max_value) {
            max_value = rows[i].simulated_tokens_per_s;
        }
    }
    return max_value;
}

static int write_figure9_svg(const char *path, const IfcSimulationRow rows[IFC_ROW_COUNT], const IfcSummary *summary) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    const int width = 1320;
    const int height = 760;
    const int plot_x = 80;
    const int plot_y = 80;
    const int plot_w = 1160;
    const int plot_h = 500;
    const double max_speed = row_max_speed(rows) * 1.12;
    const int group_w = plot_w / IFC_ROW_COUNT;
    const int bar_w = 8;

    svg_header(file, width, height, "Cambricon-LLM Figure 9 Decode-Speed Reproduction");
    fprintf(file, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" stroke=\"#202124\" stroke-width=\"1\"/>\n", plot_x, plot_y + plot_h, plot_x + plot_w, plot_y + plot_h);
    fprintf(file, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" stroke=\"#202124\" stroke-width=\"1\"/>\n", plot_x, plot_y, plot_x, plot_y + plot_h);
    for (int tick = 0; tick <= 5; ++tick) {
        double value = max_speed * (double)tick / 5.0;
        double y = (double)(plot_y + plot_h) - value / max_speed * (double)plot_h;
        fprintf(file, "<line x1=\"%d\" y1=\"%.2f\" x2=\"%d\" y2=\"%.2f\" stroke=\"#d0d5dd\" stroke-width=\"1\"/>\n", plot_x, y, plot_x + plot_w, y);
        fprintf(file, "<text x=\"%d\" y=\"%.2f\" text-anchor=\"end\" font-family=\"Arial,Helvetica,sans-serif\" font-size=\"11\" fill=\"#4b5563\">%.0f</text>\n", plot_x - 8, y + 4.0, value);
    }

    for (int i = 0; i < IFC_ROW_COUNT; ++i) {
        double x_center = (double)plot_x + (double)i * (double)group_w + (double)group_w * 0.5;
        double paper_h = rows[i].reference_tokens_per_s / max_speed * (double)plot_h;
        double sim_h = rows[i].simulated_tokens_per_s / max_speed * (double)plot_h;
        fprintf(file, "<rect x=\"%.2f\" y=\"%.2f\" width=\"%d\" height=\"%.2f\" fill=\"#4b5563\"/>\n", x_center - 11.0, (double)(plot_y + plot_h) - paper_h, bar_w, paper_h);
        fprintf(file, "<rect x=\"%.2f\" y=\"%.2f\" width=\"%d\" height=\"%.2f\" fill=\"#2f9c74\"/>\n", x_center + 3.0, (double)(plot_y + plot_h) - sim_h, bar_w, sim_h);
        fprintf(file, "<text x=\"%.2f\" y=\"%d\" text-anchor=\"end\" transform=\"rotate(-60 %.2f %d)\" font-family=\"Arial,Helvetica,sans-serif\" font-size=\"10\" fill=\"#374151\">%s/%s</text>\n", x_center + 6.0, plot_y + plot_h + 72, x_center + 6.0, plot_y + plot_h + 72, rows[i].model_label, rows[i].platform);
    }
    fprintf(file, "<text x=\"%d\" y=\"%d\" font-family=\"Arial,Helvetica,sans-serif\" font-size=\"13\" fill=\"#202124\">Paper</text><rect x=\"%d\" y=\"%d\" width=\"14\" height=\"14\" fill=\"#4b5563\"/>\n", plot_x + 20, 620, plot_x, 609);
    fprintf(file, "<text x=\"%d\" y=\"%d\" font-family=\"Arial,Helvetica,sans-serif\" font-size=\"13\" fill=\"#202124\">C simulator</text><rect x=\"%d\" y=\"%d\" width=\"14\" height=\"14\" fill=\"#2f9c74\"/>\n", plot_x + 110, 620, plot_x + 90, 609);
    fprintf(file, "<text x=\"%d\" y=\"%d\" font-family=\"Arial,Helvetica,sans-serif\" font-size=\"13\" fill=\"#202124\">Mean abs error %.3f%%, max abs error %.3f%%</text>\n", plot_x + 230, 620, summary->mean_abs_relative_error_pct, summary->max_abs_relative_error_pct);
    fprintf(file, "<text x=\"%d\" y=\"%d\" text-anchor=\"middle\" font-family=\"Arial,Helvetica,sans-serif\" font-size=\"13\" fill=\"#202124\">Decode speed (token/s)</text>\n", 28, 330);
    svg_footer(file);
    return fclose(file);
}

static int write_ablation_svg(
    const char *path,
    const IfcSimulationRow rows[IFC_ROW_COUNT],
    int tiling,
    const char *title,
    const char *baseline_label,
    const char *range_label) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    const int width = 980;
    const int height = 540;
    const int plot_x = 80;
    const int plot_y = 80;
    const int plot_w = 820;
    const int plot_h = 320;
    const int group_w = plot_w / IFC_MODEL_COUNT;
    double max_speed = 0.0;

    for (int i = 0; i < IFC_ROW_COUNT; ++i) {
        if (strcmp(rows[i].platform, "cam_llm_s") != 0) {
            continue;
        }
        double baseline = tiling ? rows[i].no_tiling_tokens_per_s : rows[i].no_read_slicing_tokens_per_s;
        if (rows[i].simulated_tokens_per_s > max_speed) {
            max_speed = rows[i].simulated_tokens_per_s;
        }
        if (baseline > max_speed) {
            max_speed = baseline;
        }
    }
    max_speed *= 1.18;

    svg_header(file, width, height, title);
    fprintf(file, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" stroke=\"#202124\" stroke-width=\"1\"/>\n", plot_x, plot_y + plot_h, plot_x + plot_w, plot_y + plot_h);
    fprintf(file, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" stroke=\"#202124\" stroke-width=\"1\"/>\n", plot_x, plot_y, plot_x, plot_y + plot_h);
    int out_i = 0;
    for (int i = 0; i < IFC_ROW_COUNT; ++i) {
        if (strcmp(rows[i].platform, "cam_llm_s") != 0) {
            continue;
        }
        double baseline = tiling ? rows[i].no_tiling_tokens_per_s : rows[i].no_read_slicing_tokens_per_s;
        double full_h = rows[i].simulated_tokens_per_s / max_speed * (double)plot_h;
        double base_h = baseline / max_speed * (double)plot_h;
        double x_center = (double)plot_x + (double)out_i * (double)group_w + (double)group_w * 0.5;
        fprintf(file, "<rect x=\"%.2f\" y=\"%.2f\" width=\"20\" height=\"%.2f\" fill=\"#2f9c74\"/>\n", x_center - 24.0, (double)(plot_y + plot_h) - full_h, full_h);
        fprintf(file, "<rect x=\"%.2f\" y=\"%.2f\" width=\"20\" height=\"%.2f\" fill=\"#9ca3af\"/>\n", x_center + 4.0, (double)(plot_y + plot_h) - base_h, base_h);
        fprintf(file, "<text x=\"%.2f\" y=\"%d\" text-anchor=\"middle\" font-family=\"Arial,Helvetica,sans-serif\" font-size=\"11\" fill=\"#374151\">%s</text>\n", x_center, plot_y + plot_h + 26, rows[i].model_label);
        fprintf(file, "<text x=\"%.2f\" y=\"%.2f\" text-anchor=\"middle\" font-family=\"Arial,Helvetica,sans-serif\" font-size=\"11\" fill=\"#202124\">%.2fx</text>\n", x_center, (double)(plot_y + plot_h) - full_h - 8.0, tiling ? rows[i].speedup_vs_no_tiling : rows[i].speedup_vs_no_read_slicing);
        ++out_i;
    }
    fprintf(file, "<rect x=\"%d\" y=\"%d\" width=\"14\" height=\"14\" fill=\"#2f9c74\"/><text x=\"%d\" y=\"%d\" font-family=\"Arial,Helvetica,sans-serif\" font-size=\"13\" fill=\"#202124\">Full simulator</text>\n", plot_x, 450, plot_x + 20, 462);
    fprintf(file, "<rect x=\"%d\" y=\"%d\" width=\"14\" height=\"14\" fill=\"#9ca3af\"/><text x=\"%d\" y=\"%d\" font-family=\"Arial,Helvetica,sans-serif\" font-size=\"13\" fill=\"#202124\">%s</text>\n", plot_x + 150, 450, plot_x + 170, 462, baseline_label);
    fprintf(file, "<text x=\"%d\" y=\"%d\" font-family=\"Arial,Helvetica,sans-serif\" font-size=\"13\" fill=\"#202124\">Paper text range: %s</text>\n", plot_x + 390, 462, range_label);
    svg_footer(file);
    return fclose(file);
}

int ifc_write_plots(const char *output_dir, const IfcSimulationRow rows[IFC_ROW_COUNT], const IfcSummary *summary) {
    char figures_dir[4096];
    char figure9_path[4096];
    char figure12_path[4096];
    char figure14_path[4096];

    join_path(figures_dir, sizeof(figures_dir), output_dir, "figures");
    if (ensure_dir(figures_dir) != 0) {
        return -1;
    }
    join_path(figure9_path, sizeof(figure9_path), figures_dir, "figure9_decode_speed.svg");
    join_path(figure12_path, sizeof(figure12_path), figures_dir, "figure12_read_slice_ablation.svg");
    join_path(figure14_path, sizeof(figure14_path), figures_dir, "figure14_tiling_ablation.svg");

    if (write_figure9_svg(figure9_path, rows, summary) != 0) {
        return -1;
    }
    if (write_ablation_svg(figure12_path, rows, 0, "Figure 12-Style Read-Slicing Ablation", "Without read slicing", "1.6x-1.8x") != 0) {
        return -1;
    }
    if (write_ablation_svg(figure14_path, rows, 1, "Figure 14-Style Hardware-Aware Tiling Ablation", "Without tiling", "1.3x-1.4x") != 0) {
        return -1;
    }
    return 0;
}

