/* qw_detector_config_dump.c -- test-only helper: load a detector.json
 * file with qw_detector_config_load() and print the parsed fields as
 * simple "key=value" lines on stdout, so the pytest suite can exercise
 * the real C JSON reader without needing a Python/C binding layer. Not
 * part of the shipped `qw` CLI; built only for `make test`.
 */
#include <stdio.h>

#include "qw_detector_config.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <detector.json>\n", argv[0]);
        return 2;
    }

    qw_detector_config_t cfg;
    if (qw_detector_config_load(argv[1], &cfg) != 0) {
        fprintf(stderr, "load_failed\n");
        return 1;
    }

    printf("min_prominence_frac=%g\n", cfg.min_prominence_frac);
    printf("min_pulse_interval_s=%g\n", cfg.min_pulse_interval_s);
    printf("trailing_margin_s=%g\n", cfg.trailing_margin_s);
    printf("max_trailing_gap_frac=%g\n", cfg.max_trailing_gap_frac);
    printf("min_trailing_pulses=%ld\n", cfg.min_trailing_pulses);
    printf("activity_weight=%g\n", cfg.activity_weight);
    printf("regularity_weight=%g\n", cfg.regularity_weight);
    printf("min_expected_bpm_floor=%g\n", cfg.min_expected_bpm_floor);
    printf("min_expected_fraction=%g\n", cfg.min_expected_fraction);
    printf("max_trusted_interval_cv=%g\n", cfg.max_trusted_interval_cv);

    return 0;
}
