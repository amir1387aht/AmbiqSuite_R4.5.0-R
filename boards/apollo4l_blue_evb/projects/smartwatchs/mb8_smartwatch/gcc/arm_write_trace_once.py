#!/usr/bin/env python3
"""Temporary: arm the current one-shot firmware after verifying its image.

Run only when the firmware has printed [NAND WRITE] READY. This tool does not
flash firmware; it compares the running target's internal MRAM to the exact
local build before writing the RAM token. It cannot verify the attached NAND.
"""
import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

BASE = 0x00018000
TOKEN = 0x6D385754
RAM_BEGIN = 0x10060000
RAM_END = 0x10160000

def jlink_path(path):
    # J-Link Commander accepts quoted paths. Reject script injection.
    text = str(path.resolve())
    if any(c in text for c in '\n\r"'):
        raise ValueError('unsupported character in path')
    return '"' + text + '"'

def invoke(jlink, lines):
    with tempfile.TemporaryDirectory(prefix='mb8-arm-') as temp:
        commands = Path(temp) / 'commands.jlink'
        commands.write_text('ExitOnError 1\nconnect\n' + '\n'.join(lines) + '\nexit\n', encoding='ascii')
        p = subprocess.run([str(jlink), '-device', 'AMA4B2KP-KXR', '-if', 'SWD', '-speed', '4000', '-NoGui', '1', '-ExitOnError', '1', '-CommandFile', str(commands)])
        if p.returncode:
            raise RuntimeError('J-Link command failed; target may be halted. No automatic retry.')

def run(elf, image, jlink, nm, objcopy):
    elf = elf.resolve(strict=True); image = image.resolve(strict=True)
    nm_out = subprocess.check_output([nm, '-S', str(elf)], text=True)
    matches = re.findall(r'^([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+[Bb]\s+mb8_write_trace_token$', nm_out, re.MULTILINE)
    if len(matches) != 1:
        raise RuntimeError('one-shot token symbol missing or ambiguous')
    address, size = (int(x, 16) for x in matches[0])
    if size != 4 or address % 4 or not (RAM_BEGIN <= address < RAM_END - 3):
        raise RuntimeError('unexpected token address; refusing to write')
    if not re.search(r'\bmb8_storage_write_trace$', nm_out, re.MULTILINE):
        raise RuntimeError('not a one-shot write trace ELF')
    data = image.read_bytes()
    if not data or len(data) > 1998848:
        raise RuntimeError('unexpected image size')
    with tempfile.TemporaryDirectory(prefix='mb8-verify-') as tmp:
        from_elf = Path(tmp) / 'from-elf.bin'; target = Path(tmp) / 'target.bin'
        subprocess.run([objcopy, '-O', 'binary', str(elf), str(from_elf)], check=True)
        if from_elf.read_bytes() != data:
            raise RuntimeError('ELF and BIN differ; rebuild and reflash the same image')
        # First session is strictly read-only: halt, read back, and resume.
        invoke(jlink, ['h', f'SaveBin {jlink_path(target)} 0x{BASE:08x} 0x{len(data):x}', 'g'])
        if not target.exists() or target.read_bytes() != data:
            raise RuntimeError('target firmware differs from local BIN; NOT armed')
        # Re-verify immediately before the RAM write; ExitOnError is mandatory.
        invoke(jlink, ['h', f'VerifyBin {jlink_path(image)} 0x{BASE:08x}',
                       f'W4 0x{address:08x} 0x{TOKEN:08x}', 'g'])
    print('Token sent once. Watch the UART for [NAND WRITE] RESULT; do not reset while it runs.')

def main():
    p = argparse.ArgumentParser(description='Arm one NAND write trace. A new file is written and a YAFFS checkpoint may be erased.')
    p.add_argument('--i-understand-this-writes-nand', action='store_true')
    p.add_argument('--jlink', required=True, help='path to JLink.exe / JLinkExe')
    p.add_argument('--elf', default='bin/mb8_smartwatch.axf')
    p.add_argument('--bin', dest='image', default='bin/mb8_smartwatch.bin')
    p.add_argument('--nm', default='arm-none-eabi-nm')
    p.add_argument('--objcopy', default='arm-none-eabi-objcopy')
    a = p.parse_args()
    if not a.i_understand_this_writes_nand:
        p.error('explicit write acknowledgment flag is required')
    try:
        run(Path(a.elf), Path(a.image), Path(a.jlink), a.nm, a.objcopy)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as e:
        print('Arm status UNKNOWN:', e, 'Do not retry until UART and target state are checked.', file=sys.stderr)
        sys.exit(1)
if __name__ == '__main__':
    main()
