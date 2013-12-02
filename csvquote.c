#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include "dbg.h"

#define READ_BUFFER_SIZE 4096
#define NON_PRINTING_FIELD_SEPARATOR 0x1F
#define NON_PRINTING_RECORD_SEPARATOR 0x1E

/*
TODO: verify that it handles multi-byte characters and unicode and utf-8 etc
*/

typedef void (*translator)(const char, const char, const char, char *);
typedef enum { HEADER_MODE, RESTORE_MODE, SANITIZE_MODE } operation_mode;

void restore(const char delimiter, const char quotechar, const char recordsep, char *c) {
    // the quotechar is not needed when restoring, but we include it
    // to keep the function parameters consistent for both translators
    switch (*c) {
        case NON_PRINTING_FIELD_SEPARATOR:
            *c = delimiter;
            break;
        case NON_PRINTING_RECORD_SEPARATOR:
            *c = recordsep;
            break;
        // no default case needed
    }
    return;
}

void sanitize(const char delimiter, const char quotechar, const char recordsep, char *c) {
    // maintain the state of quoting inside this function
    // this is OK because we need to read the file
    // sequentially (not in parallel) because the state
    // at any point depends on all of the previous data
    static bool isQuoteInEffect = false;
    static bool isMaybeEscapedQuoteChar = false;

    if (isMaybeEscapedQuoteChar) {
        if (*c != quotechar) {
            // this is the end of a quoted field
            isQuoteInEffect = false;
        }
        isMaybeEscapedQuoteChar = false;
    } else if (isQuoteInEffect) {
        if (*c == quotechar) {
            // this is either an escaped quote char or the end of a quoted
            // field. need to read one more character to decide which
            isMaybeEscapedQuoteChar = true;
        } else if (*c == delimiter) {
            *c = NON_PRINTING_FIELD_SEPARATOR;
        } else if (*c == recordsep) {
            *c = NON_PRINTING_RECORD_SEPARATOR;
        }
    } else {
        // quote not in effect
        if (*c == quotechar) {
            isQuoteInEffect = true;
        }
    }
    return;
}

int copy_file(FILE *in, const operation_mode op_mode,
const char del, const char quo, const char rec) {
    char buffer[READ_BUFFER_SIZE];
    size_t nbytes;
    char *c, *stopat;

    translator trans = sanitize; // default
    if (op_mode == RESTORE_MODE) {
        trans = restore;
    } else if (op_mode == HEADER_MODE) {
        debug("header mode goes here");
        return 0; // exit
    }

    debug("copying file with d=%d\tq=%d\tr=%d", del, quo, rec);
    while ((nbytes = fread(buffer, sizeof(char), sizeof(buffer), in)) != 0)
    {
        stopat = buffer + (nbytes);
        for (c=buffer; c<stopat; c++) {
            (*trans)(del, quo, rec, c); // no error checking inside this loop
        }
        check(fwrite(buffer, sizeof(char), nbytes, stdout) == nbytes,
            "Failed to write %zu bytes\n", nbytes);
    }
    return 0;

error:
    //if (in) { fclose(input); } // is this appropriate?
    return 1;
}

int main(int argc, char *argv[]) {
    // default parameters
    FILE *input = NULL;
    char del = ',';
    char quo = '"';
    char rec = '\n';
    operation_mode op_mode = SANITIZE_MODE;

    int opt;
    while ((opt = getopt(argc, argv, "hud:tq:r:")) != -1) {
        switch (opt) {
            case 'h':
                op_mode = HEADER_MODE;
                break;
            case 'u':
                op_mode = RESTORE_MODE;
                break;
            case 'd':
                del = optarg[0];
                break;
            case 't':
                del = '\t';
                break;
            case 'q':
                quo = optarg[0];
                break;
            case 'r':
                rec = optarg[0];
                break;
            case ':':
                // -d or -q or -r without operand
                fprintf(stderr,
                    "Option -%c requires an operand\n", optopt);
                goto usage;
            case '?':
                goto usage;
            default:
                fprintf(stderr,
                    "Unrecognized option: '-%c'\n", optopt);
                goto usage;
        }
    }

    // Process stdin or file names
    if (optind >= argc) {
        check(copy_file(stdin, op_mode, del, quo, rec) == 0,
            "failed to copy from stdin");
    } else {
        // supports multiple file names
        int i;
        for (i=optind; i<argc; i++) {
            input = fopen(argv[i], "r");
            check(input != 0, "failed to open file %s", argv[optind]);
            check(copy_file(input, op_mode, del, quo, rec) == 0,
                "failed to copy from file %s", argv[i]);
            if (input) { fclose(input); }
        }
    }

    if (input) { fclose(input); }
    return 0;

usage:
    fprintf(stderr, "Usage: %s [-hud:tq:r:] [file]\n", argv[0]);
    return 1;

error:
    if (input) { fclose(input); }
    return 1;
}
