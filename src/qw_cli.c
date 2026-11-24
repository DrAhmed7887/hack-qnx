/* qw_cli.c -- `qw <record.qw> <alarm_type> <rules.json> [detector.json]`
 *
 * Prints one JSON line to stdout:
 *   {"record": "...", "alarm": "...", "decision": "CONFIRM"|"SUPPRESS_TO_REVIEW",
 *    "reason": "...", "sqi": 0.00, "rules_status": "...", "ecg_sqi": 0.00,
 *    "pulse_sqi": 0.00}
 *
 * Exit code 0 on success, nonzero on any load/parse/validation error
 * (with a message on stderr). Both the rules file and the detector
 * engineering-constants file are validated fail-closed: a missing or
 * out-of-range field in either is a fatal error, not a silently-assumed
 * default.
 */
#include <stdio.h>
#include <string.h>
#include <libgen.h>

#include "qw_core.h"
#include "qw_detector_config.h"
#include "rules.h"

#define QW_DEFAULT_DETECTOR_CONFIG "config/detector.json"

static void copy_str(char *dst, size_t cap, const char *src) {
    if (cap == 0) return;
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void print_json_string(FILE *out, const char *s) {
    fputc('"', out);
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
            case '"': fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\n': fputs("\\n", out); break;
            case '\r': fputs("\\r", out); break;
            case '\t': fputs("\\t", out); break;
            default:
                if (c < 0x20) {
                    fprintf(out, "\\u%04x", c);
                } else {
                    fputc((char)c, out);
                }
        }
    }
    fputc('"', out);
}

static const char *record_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

int main(int argc, char **argv) {
    if (argc != 4 && argc != 5) {
        fprintf(stderr, "usage: %s <record.qw> <alarm_type> <rules.json> [detector.json]\n", argv[0]);
        return 2;
    }

    const char *record_path = argv[1];
    const char *alarm_type = argv[2];
    const char *rules_path = argv[3];
    const char *detector_config_path = (argc == 5) ? argv[4] : QW_DEFAULT_DETECTOR_CONFIG;

    qw_rules_t rules;
    if (qw_rules_load(rules_path, &rules) != 0) {
        fprintf(stderr, "error: failed to load rules file '%s'\n", rules_path);
        return 1;
    }

    qw_detector_config_t cfg;
    if (qw_detector_config_load(detector_config_path, &cfg) != 0) {
        fprintf(stderr, "error: failed to load detector config file '%s'\n", detector_config_path);
        return 1;
    }

    qw_record_t rec;
    if (qw_record_load(record_path, &rec) != 0) {
        fprintf(stderr, "error: failed to load record '%s'\n", record_path);
        return 1;
    }

    /* Stack-allocated workspace: qw_decide() is reentrant and keeps no
     * static state, so each call gets its own scratch buffers. */
    qw_decide_workspace_t ws;
    qw_decision_result_t result;
    if (qw_decide(&rec, alarm_type, &rules, &cfg, &ws, &result) != 0) {
        fprintf(stderr, "error: decide() failed for record '%s' alarm '%s'\n", record_path, alarm_type);
        qw_record_free(&rec);
        return 1;
    }

    const char *decision_str = (result.decision == QW_DECISION_CONFIRM) ? "CONFIRM" : "SUPPRESS_TO_REVIEW";

    char record_name[512];
    copy_str(record_name, sizeof(record_name), record_basename(record_path));
    /* strip a trailing .qw extension for a cleaner record id */
    size_t len = strlen(record_name);
    if (len > 3 && strcmp(record_name + len - 3, ".qw") == 0) {
        record_name[len - 3] = '\0';
    }

    printf("{\"record\": ");
    print_json_string(stdout, record_name);
    printf(", \"alarm\": ");
    print_json_string(stdout, alarm_type);
    printf(", \"decision\": ");
    print_json_string(stdout, decision_str);
    printf(", \"reason\": ");
    print_json_string(stdout, result.reason);
    printf(", \"sqi\": %.4f", result.sqi);
    printf(", \"rules_status\": ");
    print_json_string(stdout, rules.status);
    printf(", \"ecg_sqi\": %.4f, \"pulse_sqi\": %.4f}\n", result.ecg_sqi, result.pulse_sqi);

    qw_record_free(&rec);
    return 0;
}
