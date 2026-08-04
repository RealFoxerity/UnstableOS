#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <ctype.h>
#include <unistd.h>

#include "errno.h"

char * const valid_options[] = {
    "-h",
    "--help",
    "if",
    "of",
    "bs",
    "skip",
    "seek",
    "count",
    "conv",
    "iflag",
    "oflag",
    "status",
    NULL
};
char * const valid_convs[] = {
    "ascii",
    "ebcdic",
    "ibm",
    "lcase",
    "ucase",
    "swab",
    "notrunc",
    "noerror",
    NULL
};
char * const valid_iflags[] = {
    "skip_bytes",
    NULL
};
char * const valid_oflags[] = {
    "seek_bytes",
    "sync",
    NULL
};
// returns 0 if invalid
static off_t get_num(const char * s) {
    static const char mults[] = "KMGTPEZYRQ";
    const char * end_of_num = s;
    while (isdigit(*end_of_num))
        end_of_num++;
    off_t val = 1;
    char si = 0;
    if (*end_of_num == '\0')
        goto conv;
    if (*(end_of_num + 1) == '\0') {
        switch (*end_of_num) {
            case 'b':
                val = 512;
                goto conv;
            case 'c':
                goto conv;
            case 'w':
                val = 2;
                goto conv;
            default: break;
        }
    } else {
        if (strcmp(end_of_num + 1, "iB") == 0)
            si = 0;
        else if (strcmp(end_of_num + 1, "B") == 0)
            si = 1;
        else
            return -1;
    }
    int i = 0;
    for (; i < sizeof(mults) - 1 && mults[i] != *end_of_num; i++)
        val *= si ? 1000 : 1024;
    if (i == sizeof(mults) - 1)
        return -1;
    val *= si ? 1000 : 1024;
    conv:
    val *= strtoll(s, NULL, 10);
    return val;
}

static void print_help() {

}

extern void transform_block(unsigned char * block, size_t bs, uint16_t convs);

unsigned long long written_bytes = 0;
time_t start_time = 0;

void display_progress() {
    static time_t last_time = 0;
    if (last_time == 0)
        last_time = time(NULL);

    if (last_time == time(NULL)) // display each second
        return;

    time_t new_time = time(NULL);

    char unit_1 = 'B', unit_2 = 'B', unit_3 = 'B';

    unsigned long long bytes_1 = written_bytes;
    unsigned long long bytes_2 = written_bytes;
    unsigned long long avg_speed = written_bytes / (new_time - start_time);
    static const char units[] = "BKMGTPEZYRQ";
    for (int i = 1; i < sizeof(units) - 1; i++) {
        if (bytes_1 > 1024) {
            bytes_1 /= 1024;
            unit_1 = units[i];
        }
        if (bytes_2 > 1000) {
            bytes_2 /= 1000;
            unit_2 = units[i];
        }
        if (avg_speed > 1000) {
            avg_speed /= 1000;
            unit_3 = units[i];
        }
        if (bytes_1 < 1024 && bytes_2 < 1000 && avg_speed < 1000)
            break;
    }

    fprintf(stderr, "\r%llu bytes (%llu %c%s, %llu %c%s) copied, %lld seconds, %llu %c%s/s",
        written_bytes,
        bytes_1, unit_1, unit_1 == 'B'?"":"iB",
        bytes_2, unit_2, unit_2 == 'B'?"":"B",
        new_time - start_time,
        avg_speed, unit_3, unit_3 == 'B'?"":"B");
    last_time = new_time;
}

char should_exit = 0;
void signal_handler(int sig) {
    should_exit = 1;
}

int main(int argc, char ** argv) {
    char * temp;
    char * input = NULL, * output = NULL;

    char * opt = NULL;
    char do_status = 0;

    off_t bs = 512;
    unsigned long long count = -1;
    off_t skip = 0, seek = 0;
    char skip_bytes = 0, seek_bytes = 0;
    uint16_t convs = 0;

    unsigned short input_mode = O_RDONLY | O_NOCTTY;
    unsigned short output_mode = O_WRONLY | O_CREAT | O_NOCTTY;
    unsigned char trunc = 1;

    for (int i = 1; i < argc; i++) {
        char * arg = argv[i];
        int idx = getsubopt(&arg, valid_options, &opt);
        if (idx >= 2) {
            if (opt == NULL || *opt == '\0') {
                error:
                if (idx > 0)
                    fprintf(stderr, "Invalid argument to %s=\n", valid_options[idx]);
                else
                    fprintf(stderr, "Invalid argument %s\n", argv[i]);
                print_help();
                return 1;
            }
        }
        switch (idx) {
            case -1:
                goto error;
            case 0 ... 1:
                print_help();
                return 0;
            case 2:
                input = opt;
                break;
            case 3:
                output = opt;
                break;
            case 4:
                bs = get_num(opt);
                if (bs == -1)
                    goto error;
                break;
            case 5:
                skip = get_num(opt);
                if (skip == -1)
                    goto error;
                break;
            case 6:
                seek = get_num(opt);
                if (seek == -1)
                    goto error;
                break;
            case 7:
                count = get_num(opt);
                if (count == -1)
                    goto error;
                break;
            case 8:
                if (strchr(opt, '='))
                    goto error;
                while ((idx = getsubopt(&opt, valid_convs, &temp)) != -1)
                    convs |= 1 << idx;
                if (*opt != '\0')
                    goto error;
                if ((convs & 1) + ((convs & 2) >> 1) + ((convs & 4) >> 2) > 1)
                    goto error;
                if (convs & 8 && convs & 0x10)
                    goto error;
                if (convs & 0x40)
                    trunc = 0;
                break;
            case 9:
                if (strchr(opt, '='))
                    goto error;
                while ((idx = getsubopt(&opt, valid_iflags, &temp)) != -1) {
                    switch (idx) { // switch if we add more options to valid_iflags
                        case 0:
                            skip_bytes = 1;
                            break;
                        default:
                            goto error;
                    }
                }
                if (*opt != '\0')
                    goto error;
                break;
            case 10:
                if (strchr(opt, '='))
                    goto error;
                while ((idx = getsubopt(&opt, valid_oflags, &temp)) != -1) {
                    switch (idx) { // switch if we add more options to valid_iflags
                        case 0:
                            seek_bytes = 1;
                            break;
                        case 1:
                            output_mode |= O_SYNC;
                            break;
                        default:
                            goto error;
                    }
                }
                if (*opt != '\0')
                    goto error;
                break;
            case 11:
                if (strcmp(opt, "progress") != 0)
                    goto error;
                do_status = 1;
                break;
        }
    }

    int in_fd  = STDIN_FILENO;
    int out_fd = STDOUT_FILENO;

    if (!skip_bytes)
        skip *= bs;
    if (!seek_bytes)
        seek *= bs;

    if (input != NULL) {
        if ((in_fd = open(input, input_mode)) == -1) {
            fprintf(stderr, "%s: Can't open file '%s': %s\n", argv[0], input, strerror(errno));
            return 1;
        }
    }
    lseek(in_fd, skip, SEEK_SET);
    if (output != NULL) {
        if ((out_fd = open(output, output_mode, 0777)) == -1) {
            fprintf(stderr, "%s: Can't open file '%s': %s\n", argv[0], input, strerror(errno));
            return 1;
        }
    }
    lseek(out_fd, seek, SEEK_SET);
    if (trunc)
        ftruncate(out_fd, seek);

    unsigned char * block = malloc(bs);
    if (block == NULL) {
        fprintf(stderr, "%s: Can't allocate block buffer: %s\n", argv[0], strerror(errno));
        return 1;
    }
    ssize_t read_amount = 0;
    ssize_t write_amount = 0;

    unsigned long long read_blocks = 0;
    unsigned long long read_partial_blocks = 0;
    unsigned long long written_blocks = 0;
    unsigned long long written_partial_blocks = 0;

    start_time = time(NULL);
    signal(SIGINT, signal_handler);

    try_again:
    while (!should_exit &&
            read_blocks + read_partial_blocks < count &&
            (read_amount = read(in_fd, block, bs)) > 0
    ) {
        if (read_amount < bs)
            read_partial_blocks ++;
        else
            read_blocks ++;

        transform_block(block, read_amount, convs);

        if ((write_amount = write(out_fd, block, read_amount)) != bs) {
            if (write_amount == -1) {
                fprintf(stderr, "%s: error writing '%s': %s\n", argv[0], output, strerror(errno));
                fprintf(stderr, "%llu+%llu records in\n", read_blocks, read_partial_blocks);
                fprintf(stderr, "%llu+%llu records out\n", written_blocks, written_partial_blocks);
                return 1;
            }
            written_partial_blocks ++;
        } else
            written_blocks ++;
        written_bytes += write_amount;

        if (do_status)
            display_progress();
    }
    if (!should_exit && read_amount == -1) {
        if (convs & 0x80) {
            fprintf(stderr, "%s: Warning: error reading '%s': %s\n", argv[0], output, strerror(errno));
            fprintf(stderr, "%llu+%llu records in\n", read_blocks, read_partial_blocks);
            fprintf(stderr, "%llu+%llu records out\n", written_blocks, written_partial_blocks);
            goto try_again;
        }
        fprintf(stderr, "%s: error reading '%s': %s\n", argv[0], output, strerror(errno));
        fprintf(stderr, "%llu+%llu records in\n", read_blocks, read_partial_blocks);
        fprintf(stderr, "%llu+%llu records out\n", written_blocks, written_partial_blocks);
        return 1;
    }

    fprintf(stderr, "%llu+%llu records in\n", read_blocks, read_partial_blocks);
    fprintf(stderr, "%llu+%llu records out\n", written_blocks, written_partial_blocks);

    return 0;
}