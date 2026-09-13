"""Verify production menu bindings against a retail PE without running the game.

Usage: python tests/verify_campaign_menu_bindings.py path/to/DOOMEternalx64vk.exe
Requires pefile. The native Populate call is an independent check of semantics:
matching the bytes of an unrelated function was the Mission Select crash defect.
"""
from pathlib import Path
import re
import struct
import sys

import pefile


def verify(executable):
    source = (Path(__file__).resolve().parents[1] / 'src/campaign_menu_native.cpp').read_text()
    install = source.split('native_targets(uintptr_t base)', 1)[1]
    rvas = [int(value, 16) for value in re.findall(
        r'0x[0-9a-f]+', re.search(r'rvas\[\]=\{(.*?)\};', install, re.S)[1])]
    signatures = [bytes.fromhex(value) for value in re.findall(
        r'"([0-9a-f]+)"', re.search(r'bytes\[\]=\{(.*?)\};', install, re.S)[1])]
    assert len(rvas) == len(signatures) == 15
    with pefile.PE(str(executable), fast_load=True) as pe:
        for rva, signature in zip(rvas, signatures):
            assert pe.get_data(rva, len(signature)) == signature, f'prologue mismatch: {rva:x}'
        call = pe.get_data(0x10d2d99, 5)
        assert call[0] == 0xe8, 'native Populate list-copy CALL changed'
        callee = 0x10d2d9e + struct.unpack('<i', call[1:])[0]
        assert rvas[8] == callee, 'binding is not the native Populate list-copy callee'
        # Reproduce the old error: its own prologue is valid, but the actual
        # caller rejects that unrelated function as the list-copy operation.
        old_signature = bytes.fromhex('48895c240848896c2410488974241848897c242041564883ec2033f64c8bf248')
        assert pe.get_data(0x143c070, len(old_signature)) == old_signature
        assert callee != 0x143c070
        signature = bytes.fromhex(re.search(r'list_copy_signature\[\]="([0-9a-f]+)"', install)[1])
        assert pe.get_data(callee + 48, 32) == signature
        assert sum(section.get_data().count(signature) for section in pe.sections
                   if section.Characteristics & 0x20000000) == 1
    print(f'PASS 15 retail prologues; Populate CALL -> RVA {callee:#x}; former binding rejected')


if __name__ == '__main__':
    verify(Path(sys.argv[1]))
