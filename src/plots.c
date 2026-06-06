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

static double row_max_abs_error(const IfcSimulationRow rows[IFC_ROW_COUNT]) {
    double max_value = 0.0;
    for (int i = 0; i < IFC_ROW_COUNT; ++i) {
        double abs_error = fabs(rows[i].relative_error_pct);
        if (abs_error > max_value) {
            max_value = abs_error;
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

static int write_error_svg(const char *path, const IfcSimulationRow rows[IFC_ROW_COUNT]) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    const int width = 1320;
    const int height = 700;
    const int plot_x = 80;
    const int plot_y = 80;
    const int plot_w = 1160;
    const int plot_h = 420;
    const int group_w = plot_w / IFC_ROW_COUNT;
    const int bar_w = 18;
    const double limit = row_max_abs_error(rows) > 15.0 ? row_max_abs_error(rows) * 1.15 : 18.0;
    const double zero_y = (double)plot_y + (double)plot_h * 0.5;

    svg_header(file, width, height, "Figure 9 Relative Error Diagnostics");
    fprintf(file, "<line x1=\"%d\" y1=\"%.2f\" x2=\"%d\" y2=\"%.2f\" stroke=\"#202124\" stroke-width=\"1.2\"/>\n", plot_x, zero_y, plot_x + plot_w, zero_y);
    fprintf(file, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" stroke=\"#202124\" stroke-width=\"1\"/>\n", plot_x, plot_y, plot_x, plot_y + plot_h);
    for (int tick = -2; tick <= 2; ++tick) {
        double value = limit * (double)tick / 2.0;
        double y = zero_y - value / limit * ((double)plot_h * 0.5);
        fprintf(file, "<line x1=\"%d\" y1=\"%.2f\" x2=\"%d\" y2=\"%.2f\" stroke=\"#d0d5dd\" stroke-width=\"1\"/>\n", plot_x, y, plot_x + plot_w, y);
        fprintf(file, "<text x=\"%d\" y=\"%.2f\" text-anchor=\"end\" font-family=\"Arial,Helvetica,sans-serif\" font-size=\"11\" fill=\"#4b5563\">%.0f%%</text>\n", plot_x - 8, y + 4.0, value);
    }
    for (int threshold = -1; threshold <= 1; threshold += 2) {
        double y = zero_y - 15.0 * (double)threshold / limit * ((double)plot_h * 0.5);
        fprintf(file, "<line x1=\"%d\" y1=\"%.2f\" x2=\"%d\" y2=\"%.2f\" stroke=\"#b91c1c\" stroke-width=\"1\" stroke-dasharray=\"6 5\"/>\n", plot_x, y, plot_x + plot_w, y);
    }
    for (int i = 0; i < IFC_ROW_COUNT; ++i) {
        double x_center = (double)plot_x + (double)i * (double)group_w + (double)group_w * 0.5;
        double value_y = zero_y - rows[i].relative_error_pct / limit * ((double)plot_h * 0.5);
        double y = value_y < zero_y ? value_y : zero_y;
        double h = fabs(value_y - zero_y);
        const char *fill = rows[i].relative_error_pct >= 0.0 ? "#2f9c74" : "#4b5563";
        fprintf(file, "<rect x=\"%.2f\" y=\"%.2f\" width=\"%d\" height=\"%.2f\" fill=\"%s\"/>\n", x_center - (double)bar_w * 0.5, y, bar_w, h, fill);
        fprintf(file, "<text x=\"%.2f\" y=\"%d\" text-anchor=\"end\" transform=\"rotate(-60 %.2f %d)\" font-family=\"Arial,Helvetica,sans-serif\" font-size=\"10\" fill=\"#374151\">%s/%s</text>\n", x_center + 4.0, plot_y + plot_h + 76, x_center + 4.0, plot_y + plot_h + 76, rows[i].model_label, rows[i].platform);
    }
    fprintf(file, "<text x=\"%d\" y=\"%d\" font-family=\"Arial,Helvetica,sans-serif\" font-size=\"13\" fill=\"#202124\">Dashed red lines mark +/-15%% reproduction bound.</text>\n", plot_x, 640);
    svg_footer(file);
    return fclose(file);
}

static int write_platform_summary_svg(const char *path, const IfcSimulationRow rows[IFC_ROW_COUNT]) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    const int width = 980;
    const int height = 540;
    const int plot_x = 90;
    const int plot_y = 80;
    const int plot_w = 760;
    const int plot_h = 320;
    double mean_abs[IFC_PLATFORM_COUNT];
    double max_abs[IFC_PLATFORM_COUNT];
    double max_y = 0.0;

    for (int platform_id = 0; platform_id < IFC_PLATFORM_COUNT; ++platform_id) {
        double sum = 0.0;
        int count = 0;
        max_abs[platform_id] = 0.0;
        for (int i = 0; i < IFC_ROW_COUNT; ++i) {
            if (strcmp(rows[i].platform, IFC_PLATFORMS[platform_id].name) != 0) {
                continue;
            }
            double abs_error = fabs(rows[i].relative_error_pct);
            sum += abs_error;
            if (abs_error > max_abs[platform_id]) {
                max_abs[platform_id] = abs_error;
            }
            ++count;
        }
        mean_abs[platform_id] = sum / (double)count;
        if (max_abs[platform_id] > max_y) {
            max_y = max_abs[platform_id];
        }
    }
    max_y *= 1.25;

    svg_header(file, width, height, "Platform-Level Figure 9 Error Summary");
    fprintf(file, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" stroke=\"#202124\" stroke-width=\"1\"/>\n", plot_x, plot_y + plot_h, plot_x + plot_w, plot_y + plot_h);
    fprintf(file, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" stroke=\"#202124\" stroke-width=\"1\"/>\n", plot_x, plot_y, plot_x, plot_y + plot_h);
    for (int tick = 0; tick <= 5; ++tick) {
        double value = max_y * (double)tick / 5.0;
        double y = (double)(plot_y + plot_h) - value / max_y * (double)plot_h;
        fprintf(file, "<line x1=\"%d\" y1=\"%.2f\" x2=\"%d\" y2=\"%.2f\" stroke=\"#d0d5dd\" stroke-width=\"1\"/>\n", plot_x, y, plot_x + plot_w, y);
        fprintf(file, "<text x=\"%d\" y=\"%.2f\" text-anchor=\"end\" font-family=\"Arial,Helvetica,sans-serif\" font-size=\"11\" fill=\"#4b5563\">%.1f%%</text>\n", plot_x - 8, y + 4.0, value);
    }
    for (int platform_id = 0; platform_id < IFC_PLATFORM_COUNT; ++platform_id) {
        double x_center = (double)plot_x + ((double)platform_id + 0.5) * (double)plot_w / (double)IFC_PLATFORM_COUNT;
        double mean_h = mean_abs[platform_id] / max_y * (double)plot_h;
        double max_h = max_abs[platform_id] / max_y * (double)plot_h;
        fprintf(file, "<rect x=\"%.2f\" y=\"%.2f\" width=\"32\" height=\"%.2f\" fill=\"#2f9c74\"/>\n", x_center - 42.0, (double)(plot_y + plot_h) - mean_h, mean_h);
        fprintf(file, "<rect x=\"%.2f\" y=\"%.2f\" width=\"32\" height=\"%.2f\" fill=\"#4b5563\"/>\n", x_center + 10.0, (double)(plot_y + plot_h) - max_h, max_h);
        fprintf(file, "<text x=\"%.2f\" y=\"%d\" text-anchor=\"middle\" font-family=\"Arial,Helvetica,sans-serif\" font-size=\"12\" fill=\"#374151\">%s</text>\n", x_center, plot_y + plot_h + 28, IFC_PLATFORMS[platform_id].label);
        fprintf(file, "<text x=\"%.2f\" y=\"%.2f\" text-anchor=\"middle\" font-family=\"Arial,Helvetica,sans-serif\" font-size=\"11\" fill=\"#202124\">%.2f%%</text>\n", x_center - 26.0, (double)(plot_y + plot_h) - mean_h - 8.0, mean_abs[platform_id]);
    }
    fprintf(file, "<rect x=\"%d\" y=\"%d\" width=\"14\" height=\"14\" fill=\"#2f9c74\"/><text x=\"%d\" y=\"%d\" font-family=\"Arial,Helvetica,sans-serif\" font-size=\"13\" fill=\"#202124\">Mean abs error</text>\n", plot_x, 462, plot_x + 20, 474);
    fprintf(file, "<rect x=\"%d\" y=\"%d\" width=\"14\" height=\"14\" fill=\"#4b5563\"/><text x=\"%d\" y=\"%d\" font-family=\"Arial,Helvetica,sans-serif\" font-size=\"13\" fill=\"#202124\">Max abs error</text>\n", plot_x + 170, 462, plot_x + 190, 474);
    svg_footer(file);
    return fclose(file);
}

static double timeline_x(double start_us, double total_us, int plot_x, int plot_w) {
    return (double)plot_x + start_us / total_us * (double)plot_w;
}

static int write_controller_timeline_svg(const char *path) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    const IfcPlatformProfile *platform = &IFC_PLATFORMS[0];
    IfcTileModel tile = ifc_derive_tile_model(platform);
    const int width = 1120;
    const int height = 540;
    const int plot_x = 90;
    const int plot_y = 80;
    const int plot_w = 940;
    const int row_h = 34;
    const double channel_transfer_us = tile.tile_width / (double)platform->channels / platform->channel_bandwidth_Bps * 1e6;
    const double read_compute_us = platform->array_read_us + channel_transfer_us;
    const double slice_us = ((double)platform->page_bytes / (double)IFC_READ_SLICES_PER_REQUEST) / platform->channel_bandwidth_Bps * 1e6;
    const double total_us = read_compute_us * 8.0;

    svg_header(file, width, height, "Sample Cambricon-LLM-S Controller Timeline");
    fprintf(file, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" stroke=\"#202124\" stroke-width=\"1\"/>\n", plot_x, plot_y + row_h * platform->channels + 8, plot_x + plot_w, plot_y + row_h * platform->channels + 8);
    for (int tick = 0; tick <= 4; ++tick) {
        double t_us = total_us * (double)tick / 4.0;
        double x = timeline_x(t_us, total_us, plot_x, plot_w);
        fprintf(file, "<line x1=\"%.2f\" y1=\"%d\" x2=\"%.2f\" y2=\"%d\" stroke=\"#d0d5dd\" stroke-width=\"1\"/>\n", x, plot_y - 10, x, plot_y + row_h * platform->channels + 8);
        fprintf(file, "<text x=\"%.2f\" y=\"%d\" text-anchor=\"middle\" font-family=\"Arial,Helvetica,sans-serif\" font-size=\"11\" fill=\"#4b5563\">%.0f us</text>\n", x, plot_y + row_h * platform->channels + 30, t_us);
    }
    for (int channel = 0; channel < platform->channels; ++channel) {
        int y = plot_y + channel * row_h;
        fprintf(file, "<text x=\"%d\" y=\"%d\" text-anchor=\"end\" font-family=\"Arial,Helvetica,sans-serif\" font-size=\"11\" fill=\"#374151\">ch%d</text>\n", plot_x - 10, y + 20, channel);
        fprintf(file, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" stroke=\"#eef2f7\" stroke-width=\"1\"/>\n", plot_x, y + 16, plot_x + plot_w, y + 16);
        for (int tile_id = 0; tile_id < 8; ++tile_id) {
            double start_us = (double)tile_id * read_compute_us;
            double rc_x = timeline_x(start_us, total_us, plot_x, plot_w);
            double rc_w = read_compute_us / total_us * (double)plot_w;
            fprintf(file, "<rect x=\"%.2f\" y=\"%d\" width=\"%.2f\" height=\"14\" fill=\"#2f9c74\" opacity=\"0.75\"/>\n", rc_x, y + 4, rc_w);
            if ((tile_id + 1) % IFC_READ_SLICES_PER_REQUEST == 0) {
                double slice_start = start_us + channel_transfer_us;
                for (int slice = 0; slice < IFC_READ_SLICES_PER_REQUEST; ++slice) {
                    double sx = timeline_x(slice_start + (double)slice * slice_us, total_us, plot_x, plot_w);
                    double sw = slice_us / total_us * (double)plot_w;
                    fprintf(file, "<rect x=\"%.2f\" y=\"%d\" width=\"%.2f\" height=\"8\" fill=\"#4b5563\" opacity=\"0.82\"/>\n", sx, y + 22, sw);
                }
            }
        }
    }
    fprintf(file, "<rect x=\"%d\" y=\"%d\" width=\"14\" height=\"14\" fill=\"#2f9c74\" opacity=\"0.75\"/><text x=\"%d\" y=\"%d\" font-family=\"Arial,Helvetica,sans-serif\" font-size=\"13\" fill=\"#202124\">READ_COMPUTE service window</text>\n", plot_x, 448, plot_x + 20, 460);
    fprintf(file, "<rect x=\"%d\" y=\"%d\" width=\"14\" height=\"8\" fill=\"#4b5563\" opacity=\"0.82\"/><text x=\"%d\" y=\"%d\" font-family=\"Arial,Helvetica,sans-serif\" font-size=\"13\" fill=\"#202124\">READ_SLICE channel transfers inserted after each four tiled requests</text>\n", plot_x, 478, plot_x + 20, 486);
    fprintf(file, "<text x=\"%d\" y=\"%d\" font-family=\"Arial,Helvetica,sans-serif\" font-size=\"12\" fill=\"#4b5563\">Sample uses OPT-6.7B/Cambricon-LLM-S schedule dimensions: %.0fx%.0f tile, %.3f us channel transfer, %.3f us slice transfer.</text>\n", plot_x, 514, tile.tile_height, tile.tile_width, channel_transfer_us, slice_us);
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
    char error_path[4096];
    char platform_path[4096];
    char timeline_path[4096];
    char figure12_path[4096];
    char figure14_path[4096];

    join_path(figures_dir, sizeof(figures_dir), output_dir, "figures");
    if (ensure_dir(figures_dir) != 0) {
        return -1;
    }
    join_path(figure9_path, sizeof(figure9_path), figures_dir, "figure9_decode_speed.svg");
    join_path(error_path, sizeof(error_path), figures_dir, "figure9_relative_error.svg");
    join_path(platform_path, sizeof(platform_path), figures_dir, "platform_error_summary.svg");
    join_path(timeline_path, sizeof(timeline_path), figures_dir, "controller_schedule_timeline.svg");
    join_path(figure12_path, sizeof(figure12_path), figures_dir, "figure12_read_slice_ablation.svg");
    join_path(figure14_path, sizeof(figure14_path), figures_dir, "figure14_tiling_ablation.svg");

    if (write_figure9_svg(figure9_path, rows, summary) != 0) {
        return -1;
    }
    if (write_error_svg(error_path, rows) != 0) {
        return -1;
    }
    if (write_platform_summary_svg(platform_path, rows) != 0) {
        return -1;
    }
    if (write_controller_timeline_svg(timeline_path) != 0) {
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
