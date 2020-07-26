#!/usr/bin/python3
#
# BLE garage door opener remote control
#
# Copyright (C) 2020, Stephan <kiffie@mailbox.org>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#
# Create a C source file and/or a text file containing the Receiver Master Key
#

import argparse
import base64
import zlib
import secrets

parser = argparse.ArgumentParser()
parser.add_argument('keyfile', help='keyfile (binary)')
parser.add_argument('-c', nargs=2, metavar=('template', 'output'),
                    help='generate C file from a template')
parser.add_argument('-t', metavar='output',
                    help='generate textual output')
parser.add_argument('--keygen', action='store_const', const=True,
                    help='generate new binary key file (20 bytes)')

args = parser.parse_args()

if args.keygen:
    key = secrets.token_bytes(20)
    with open(args.keyfile, 'xb') as f:
        f.write(key)
else:
    with open(args.keyfile, 'rb') as f:
        key = f.read()

if args.t:
    with open(args.t, 'w') as f:
        keytxt = ""
        for (i, c) in enumerate(base64.b32encode(key)):
            keytxt += chr(c)
            if (i + 1) % 5 == 0:
                keytxt += ' '
        crc = zlib.crc32(key)
        print("Base32 encoded Receiver Master Key with CRC-32 checksum\n", file=f)
        print("key:      {}".format(keytxt.lower()), file=f)
        print("length:   {}".format(len(key)), file=f)
        print("checksum: {:08x}".format(crc), file=f)

if args.c:
    (template_file, output_file) = args.c
    with open(template_file, 'r') as f:
        template = f.read()
    with open(output_file, 'w') as f:
        for line in template.splitlines():
            if line.lstrip().startswith('${RXM_KEY}'):
                ident = line.find('${RXM_KEY}')
                assert(ident >= 0)
                for (i, byte) in enumerate(key):
                    if i % 8 == 0:  # start of line
                        for _ in range(ident):
                            print(' ', end='', file=f)
                    print("0x{:02x}".format(byte), end='', file=f)
                    if i == len(key) - 1:   # last byte
                        print(file=f)
                    elif i % 8 == 7:    # end of line
                        print(',', file=f)
                    else:
                        print(', ', end='', file=f)
            else:
                print(line, file=f)

# end
