#!/usr/bin/env python3
#
# Copyright (c) 2022 Meta
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import os
import re


def front_matter(sys_nerr):
    return f'''
/*
 * This file generated by {__file__}
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/sys/util.h>

#define sys_nerr {sys_nerr}'''


def gen_strerror_table(input, output):
    with open(input, 'r') as inf:

        highest_errno = 0
        symbols = []
        msgs = {}

        for line in inf.readlines():
            # Select items of the form below (note: ERRNO is numeric)
            # #define SYMBOL ERRNO /**< MSG */
            pat = r'^#define[\s]+(E[A-Z_]*)[\s]+([1-9][0-9]*)[\s]+/\*\*<[\s]+(.*)[\s]+\*/[\s]*$'
            match = re.match(pat, line)

            if not match:
                continue

            symbol = match[1]
            errno = int(match[2])
            msg = match[3]

            symbols.append(symbol)
            msgs[symbol] = msg

            highest_errno = max(int(errno), highest_errno)

        try:
            os.makedirs(os.path.dirname(output))
        except BaseException:
            # directory already present
            pass

        with open(output, 'w') as outf:

            print(front_matter(highest_errno + 1), file=outf)

            # Generate string table
            print(
                f'static const char *const sys_errlist[sys_nerr] = {{', file=outf)
            print('[0] = "Success",', file=outf)
            for symbol in symbols:
                print(f'[{symbol}] = "{msgs[symbol]}",', file=outf)

            print('};', file=outf)

            # Generate string lengths (includes trailing '\0')
            print(
                f'static const uint8_t sys_errlen[sys_nerr] = {{', file=outf)
            print('[0] = 8,', file=outf)
            for symbol in symbols:
                print(f'[{symbol}] = {len(msgs[symbol]) + 1},', file=outf)

            print('};', file=outf)


def parse_args():
    parser = argparse.ArgumentParser(allow_abbrev=False)
    parser.add_argument(
        '-i',
        '--input',
        dest='input',
        required=True,
        help='input file (e.g. lib/libc/minimal/include/errno.h)')
    parser.add_argument(
        '-o',
        '--output',
        dest='output',
        required=True,
        help='output file (e.g. build/zephyr/misc/generated/libc/minimal/strerror_table.h)')

    args = parser.parse_args()

    return args


def main():
    args = parse_args()
    gen_strerror_table(args.input, args.output)


if __name__ == '__main__':
    main()
