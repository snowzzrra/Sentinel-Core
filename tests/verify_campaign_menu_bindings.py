"""Verify production menu bindings against a retail PE without running the game.

Usage: python tests/verify_campaign_menu_bindings.py path/to/DOOMEternalx64vk.exe
Requires pefile. The native Populate call independently checks binding semantics.
"""
from pathlib import Path
import re
import struct
import sys

import pefile


def verify(executable):
    root = Path(__file__).resolve().parents[1]
    source = (root / 'src/campaign_menu_native.cpp').read_text()
    runtime = (root / 'src/native_runtime.cpp').read_text()
    assert runtime.index('campaign_menu::validate_native_targets(') < runtime.index('special::install(binding, stop_event)')
    assert runtime.index('special::install(binding, stop_event)') < runtime.index('campaign_menu::install(binding,stop_event)')
    assert 'validate_native_targets(' not in source.split('bool install(const engine::Binding& binding,HANDLE)', 1)[1]
    install = source.split('native_targets(uintptr_t base)', 1)[1]
    rvas = [int(value, 16) for value in re.findall(
        r'0x[0-9a-f]+', re.search(r'rvas\[\]=\{(.*?)\};', install, re.S)[1])]
    signatures = [bytes.fromhex(value) for value in re.findall(
        r'"([0-9a-f]+)"', re.search(r'bytes\[\]=\{(.*?)\};', install, re.S)[1])]
    assert len(rvas) == len(signatures) == 43
    with pefile.PE(str(executable), fast_load=True) as pe:
        for rva, signature in zip(rvas, signatures):
            assert pe.get_data(rva, len(signature)) == signature, f'prologue mismatch: {rva:x}'
        call = pe.get_data(0x10d2d99, 5)
        assert call[0] == 0xe8, 'native Populate list-copy CALL changed'
        callee = 0x10d2d9e + struct.unpack('<i', call[1:])[0]
        assert rvas[8] == callee, 'binding is not the native Populate list-copy callee'
        old_signature = bytes.fromhex('48895c240848896c2410488974241848897c242041564883ec2033f64c8bf248')
        assert pe.get_data(0x143c070, len(old_signature)) == old_signature
        assert callee != 0x143c070
        signature = bytes.fromhex(re.search(r'list_copy_signature\[\]="([0-9a-f]+)"', install)[1])
        assert pe.get_data(callee + 48, 32) == signature
        assert sum(section.get_data().count(signature) for section in pe.sections
                   if section.Characteristics & 0x20000000) == 1
    print(f'PASS 43 retail prologues; Populate CALL -> RVA {callee:#x}; unrelated binding rejected')


if __name__ == '__main__':
    verify(Path(sys.argv[1]))
