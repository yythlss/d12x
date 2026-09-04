#!/usr/bin/env python3
# -*- coding:utf-8 -*-
# SPDX-License-Identifier: Apache-2.0
#
# Dehuang.Wu
# Copyright (C) 2021-2026 ArtInChip Technology Co., Ltd

import os
import re
import sys
import json
import platform
import argparse
import subprocess
from collections import OrderedDict
import shutil


def run_cmd(cmdstr):
    print(cmdstr)
    cmd = cmdstr.split(' ')
    ret = subprocess.run(cmd, stdout=subprocess.PIPE)
    if ret.returncode != 0:
        sys.exit(1)


def align_up(size, align):
    return align * int((size + align - 1) / align)


def trim_trailing_ff(imgfile, align):
    rawfile = imgfile + '.raw'
    shutil.copyfile(imgfile, rawfile)

    filesize = os.path.getsize(imgfile)
    last_data_pos = -1
    chunk_size = 1024 * 1024

    with open(imgfile, 'rb') as f:
        pos = filesize
        while pos > 0:
            read_size = min(chunk_size, pos)
            pos -= read_size
            f.seek(pos)
            data = f.read(read_size)
            for index in range(len(data) - 1, -1, -1):
                if data[index] != 0xFF:
                    last_data_pos = pos + index
                    break
            if last_data_pos >= 0:
                break

    if last_data_pos < 0:
        trim_size = align
    else:
        trim_size = align_up(last_data_pos + 1, align)
        if trim_size > filesize:
            trim_size = filesize

    with open(imgfile, 'rb+') as f:
        f.truncate(trim_size)

    print('Backup raw image: {}'.format(rawfile))
    print('Trim trailing 0xFF: {} -> {}'.format(filesize, trim_size))


def mkimage_get_resource_size(srcdir, block_siz):
    total_size = 0
    root_path = srcdir
    for root, dirs, files in os.walk(srcdir):
        for fn in files:
            fpath = os.path.join(root, fn)
            size = os.path.getsize(fpath)
            size = block_siz * int(round((size + block_siz - 1) / block_siz))
            total_size += size
    return total_size


def mkimage_get_mtdpart_size(outfile):
    imgname = os.path.basename(outfile)
    partlist = os.path.join(os.path.dirname(outfile), 'partition_file_list.h')
    size = 0
    if not os.path.exists(partlist):
        return 0
    with open(partlist) as f:
        lines = f.readlines()
        for ln in lines:
            ln = ln.strip()
            if not ln:
                continue
            parts = ln.split(',')
            if len(parts) < 3:
                continue
            name = parts[1].replace('"', '').replace('*', '')
            if any(imgname == re.sub(pat, "", name) for pat in [".enc", ".sparse.enc", ".sparse"]):
                size = int(parts[2])
                return size
    print('Image {} is not used in any partition'.format(imgname))
    print('please check your project\'s image_cfg.json')
    return size


def check_is_nftl_part(outfile):
    imgname = os.path.basename(outfile)
    partjson = os.path.join(os.path.dirname(outfile), 'partition.json')
    if not os.path.exists(partjson):
        return False

    with open(partjson) as f:
        cfg = json.load(f, object_pairs_hook=OrderedDict)

    nftl_value = cfg["partitions"].get("nftl")
    if nftl_value is None:
        return False

    imgname = os.path.basename(outfile)
    parts = imgname.split('.')
    nftl_part = parts[0] + ':'
    if nftl_part in nftl_value:
        return True
    return False


def main(args):
    inputdir = args.inputdir
    # First priority is pack folder in the same directory with output file
    inputdir_1st = os.path.join(os.path.dirname(args.outfile), inputdir)
    if os.path.exists(inputdir_1st):
        inputdir = inputdir_1st
    if os.path.exists(inputdir) is False:
        print('Info: inputdir {} is not exist, use a empty one.'.format(inputdir))
        inputdir = os.path.dirname(args.outfile) + '/empty'
        os.makedirs(inputdir, exist_ok=True)
    print(inputdir)
    if args.imgsize is not None:
        imgsize = int(args.imgsize)
    else:
        imgsize = mkimage_get_mtdpart_size(args.outfile)

    if imgsize == 0:
        imgsize = mkimage_get_resource_size(inputdir, int(args.blocksize))
        # Add some free space
        imgsize += (4 * int(args.blocksize))

    # Space reserved for bad block management in NFTL
    if check_is_nftl_part(args.outfile):
        if args.reserved and (int(args.reserved) > 50):
            NFTL_RESERVED = (int(args.reserved) + 1) * 64 * 2048
        else:
            NFTL_RESERVED = 51 * 64 * 2048
        imgsize -= NFTL_RESERVED
        if imgsize < 0:
            print('Error, partition size is less than NFTL reserved: {}'.format(NFTL_RESERVED))
            sys.exit(1)
        # Round to block alignment
        blocksize = int(args.blocksize)
        imgsize = blocksize * ((imgsize + blocksize - 1) // blocksize)

    cmdstr = args.tooldir
    if platform.system() == 'Linux':
        cmdstr += 'mklittlefs '
    elif platform.system() == 'Windows':
        cmdstr += 'mklittlefs.exe '
    cmdstr += '-c {} '.format(inputdir)
    cmdstr += '-b {} '.format(args.blocksize)
    cmdstr += '-p {} '.format(args.pagesize)
    cmdstr += '-s {} '.format(imgsize)
    cmdstr += '{}'.format(args.outfile)
    run_cmd(cmdstr)
    trim_trailing_ff(args.outfile, int(args.blocksize))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("-i", "--inputdir", type=str,
                        help="input directory")
    parser.add_argument("-o", "--outfile", type=str,
                        help="output file")
    parser.add_argument("-p", "--pagesize", type=str,
                        help="page size")
    parser.add_argument("-b", "--blocksize", type=str,
                        help="block size")
    parser.add_argument("-t", "--tooldir", type=str, required=True,
                        help="tool directory")
    parser.add_argument("-g", "--imgsize", type=str,
                        help="image size")
    parser.add_argument("-r", "--reserved", type=str,
                        help="reserved block count for NFTL")
    args = parser.parse_args()
    main(args)
