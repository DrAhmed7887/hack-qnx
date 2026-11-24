/* qw_rules_dump.c -- test-only helper: load a rules.json file with
 * qw_rules_load() and print the parsed fields as simple "key=value" lines
 * on stdout, so the pytest suite can exercise the real C JSON reader
 * without needing a Python/C binding layer. Not part of the shipped `qw`
 * CLI; built only for `make test`.
 */
#include <stdio.h>
#include <string.h>

#include "rules.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <rules.json> [never_suppress_query ...]\n", argv[0]);
        return 2;
    }

    qw_rules_t rules;
    if (qw_rules_load(argv[1], &rules) != 0) {
        fprintf(stderr, "load_failed\n");
        return 1;
    }

    printf("rules.status=%s\n", rules.status);
    printf("n_alarms=%zu\n", rules.n_alarms);
    for (size_t i = 0; i < rules.n_alarms; i++) {
        const qw_alarm_def_t *a = &rules.alarms[i];
        printf("alarm.%s.has_no_beat_s=%d\n", a->name, a->has_no_beat_s);
        if (a->has_no_beat_s) printf("alarm.%s.no_beat_s=%g\n", a->name, a->no_beat_s);
        printf("alarm.%s.has_hr_below_bpm=%d\n", a->name, a->has_hr_below_bpm);
        if (a->has_hr_below_bpm) printf("alarm.%s.hr_below_bpm=%g\n", a->name, a->hr_below_bpm);
        printf("alarm.%s.has_hr_above_bpm=%d\n", a->name, a->has_hr_above_bpm);
        if (a->has_hr_above_bpm) printf("alarm.%s.hr_above_bpm=%g\n", a->name, a->hr_above_bpm);
        printf("alarm.%s.has_beats=%d\n", a->name, a->has_beats);
        if (a->has_beats) printf("alarm.%s.beats=%ld\n", a->name, a->beats);
        printf("alarm.%s.has_duration_s=%d\n", a->name, a->has_duration_s);
        if (a->has_duration_s) printf("alarm.%s.duration_s=%g\n", a->name, a->duration_s);
    }

    printf("suppression.window_s=%g\n", rules.suppression.window_s);
    printf("suppression.min_signal_quality=%g\n", rules.suppression.min_signal_quality);
    printf("suppression.n_never_suppress=%zu\n", rules.suppression.n_never_suppress);
    for (size_t i = 0; i < rules.suppression.n_never_suppress; i++) {
        printf("suppression.never_suppress[%zu]=%s\n", i, rules.suppression.never_suppress[i]);
    }

    for (int i = 2; i < argc; i++) {
        printf("never_suppress(%s)=%d\n", argv[i], qw_rules_never_suppress(&rules, argv[i]));
    }

    return 0;
}
