/* qw_cli.c -- `qw <record.qw> <alarm_type> <rules.json>`
 *
 * Prints one JSON line to stdout:
 *   {"record": "...", "alarm": "...", "decision": "CONFIRM"|"SUPPRESS_TO_REVIEW",
 *    "reason": "...", "sqi": 0.00}
 *
 * Exit code 0 on success, nonzero on any load/parse error (with a message
 * on stderr).
 */
#include <stdio.h>
#include <string.h>
#include <libgen.h>

#include "qw_core.h"
#include "rules.h"

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
    if (argc != 4) {
        fprintf(stderr, "usage: %s <record.qw> <alarm_type> <rules.json>\n", argv[0]);
        return 2;
    }

    const char *record_path = argv[1];
    const char *alarm_type = argv[2];
    const char *rules_path = argv[3];

    qw_rules_t rules;
    if (qw_rules_load(rules_path, &rules) != 0) {
        fprintf(stderr, "error: failed to load rules file '%s'\n", rules_path);
        return 1;
    }

    qw_record_t rec;
    if (qw_record_load(record_path, &rec) != 0) {
        fprintf(stderr, "error: failed to load record '%s'\n", record_path);
        return 1;
    }

    qw_decision_result_t result;
    if (qw_decide(&rec, alarm_type, &rules, &result) != 0) {
        fprintf(stderr, "error: decide() failed for record '%s' alarm '%s'\n", record_path, alarm_type);
        qw_record_free(&rec);
        return 1;
    }

    const char *decision_str = (result.decision == QW_DECISION_CONFIRM) ? "CONFIRM" : "SUPPRESS_TO_REVIEW";

    char record_name[512];
    strncpy(record_name, record_basename(record_path), sizeof(record_name) - 1);
    record_name[sizeof(record_name) - 1] = '\0';
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
    printf(", \"sqi\": %.4f}\n", result.sqi);

    qw_record_free(&rec);
    return 0;
}
