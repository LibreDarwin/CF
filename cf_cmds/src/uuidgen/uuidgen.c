/*
 * Copyright (C) 2026, LibreDarwin
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Clean-room reimplementation of uuidgen (Darwin/macOS)
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <uuid/uuid.h>
#include <xlocale.h>

#define UUID_STRLEN 37

static void usage(void) {
    fwrite("generate a universally unique identifier\n", 1, 0x29, stderr);
    fwrite("usage: uuidgen [-hdr]\n", 1, 0x16, stderr);
    fwrite("\t-hdr\temit result in form suitable for copying into a header\n", 1, 0x3d, stderr);
}

int main(int argc, char *argv[]) {
    uuid_t uuid;
    char out[UUID_STRLEN];
    char *env;

    if (argc >= 3) {
        usage();
        return 1;
    }

    env = getenv("CFUUIDVersionNumber");
    if (env != NULL && strtoul_l(env, NULL, 0, NULL) == 1) {
        uuid_generate_time(uuid);
    } else {
        uuid_generate(uuid);
    }
    uuid_unparse_upper(uuid, out);

    if (argc == 2) {
        if (argv[1] == NULL || strcmp(argv[1], "-hdr") != 0) {
            usage();
            return 1;
        }
        fprintf(stdout, "// %s\n", out);
        fwrite("#warning Change the macro name MYUUID below to something useful!\n",
               1, 0x41, stdout);
        fprintf(stdout,
                "#define MYUUID CFUUIDGetConstantUUIDWithBytes(kCFAllocatorSystemDefault, "
                "0x%.2s, 0x%.2s, 0x%.2s, 0x%.2s, 0x%.2s, 0x%.2s, 0x%.2s, "
                "0x%.2s, 0x%.2s, 0x%.2s, 0x%.2s, 0x%.2s, 0x%.2s, 0x%.2s, "
                "0x%.2s, 0x%.2s)\n",
                out, out + 2, out + 4, out + 6, out + 9, out + 11, out + 14,
                out + 16, out + 19, out + 21, out + 24, out + 26, out + 28,
                out + 30, out + 32, out + 34);
    } else {
        fprintf(stdout, "%s\n", out);
    }

    return 0;
}
