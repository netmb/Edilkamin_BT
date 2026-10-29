#!/usr/bin/env python3
"""Package the pinned Wemos build; never commit, push or flash implicitly."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

root = Path(__file__).resolve().parent.parent
build = root / '.pio/build/wemos_d1_mini32'
web = root / 'webflash'
version = re.search(r'#define EDILKAMIN_VERSION "([^"]+)"',
                    (root / 'src/FirmwareVersion.h').read_text()).group(1)
for name in ('bootloader.bin', 'partitions.bin', 'firmware.bin'):
    shutil.copyfile(build / name, web / name)
# Both generated images must already match board flash mode/frequency/size.
for name in ('bootloader.bin', 'firmware.bin'):
    header = (web / name).read_bytes()[:4]
    assert header[0] == 0xe9 and header[2:4] == bytes.fromhex('0220'), name
assert (web / 'firmware.bin').stat().st_size <= 0x250000
base = {'name': 'Edilkamin_BT Firmware', 'version': version,
        'new_install_prompt_erase': True, 'new_install_improv_wait_time': 0}
# The single visible installer uses the original multipart layout. Keeping the
# gaps unwritten preserves NVS/LittleFS when the user does not choose erase.
standard_parts = [{'path': 'bootloader.bin', 'offset': 4096},
                  {'path': 'partitions.bin', 'offset': 32768},
                  {'path': 'firmware.bin', 'offset': 65536}]
for filename, parts in [('manifest.json', standard_parts),
                        ('manifest-full.json', [{'path': 'merged-firmware.bin', 'offset': 0}])]:
    manifest = dict(base, builds=[{'chipFamily': 'ESP32', 'parts': parts}])
    (web / filename).write_text(json.dumps(manifest, indent=2) + '\n')
merged = (web / 'merged-firmware.bin').read_bytes()
for name, offset in [('bootloader.bin', 0x1000), ('partitions.bin', 0x8000), ('firmware.bin', 0x10000)]:
    data = (web / name).read_bytes()
    assert merged[offset:offset+len(data)] == data, name
metadata = {'version': version, 'hardware_validation': {'status': 'pending'},
            'packaged_from_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
            'source_sha256': {}, 'files': {}}
for path in sorted((root / 'src').glob('*')):
    if path.is_file(): metadata['source_sha256'][str(path.relative_to(root))] = hashlib.sha256(path.read_bytes()).hexdigest()
for name in ('platformio.ini', 'partitions.csv'):
    metadata['source_sha256'][name] = hashlib.sha256((root / name).read_bytes()).hexdigest()
for name in ('bootloader.bin', 'partitions.bin', 'firmware.bin', 'merged-firmware.bin'):
    data = (web / name).read_bytes()
    metadata['files'][name] = {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
validation_path = root / 'docs/hardware-validation.json'
if validation_path.exists():
    validation = json.loads(validation_path.read_text())
    if (validation.get('version') == version and
        validation.get('firmware_sha256') == metadata['files']['firmware.bin']['sha256'] and
        validation.get('single_fan_cold_test') == 'passed'):
        metadata['hardware_validation'] = {
            'status': 'single_fan_cold_test_passed', 'date': validation['date'],
            'report': '../docs/hardware-validation.json',
            'not_tested': validation['not_tested']}
    elif validation.get('single_fan_cold_test') == 'passed':
        # Keep the historical binary identity; transfer only the demonstrated
        # control-code coverage when the version header is the sole difference.
        excluded = 'src/FirmwareVersion.h'
        old = {k: v for k, v in validation.get('source_sha256', {}).items() if k != excluded}
        current = {k: v for k, v in metadata['source_sha256'].items() if k != excluded}
        if old and old == current:
            metadata['hardware_validation'] = {
                'status': 'version_only_rebuild_of_validated_source',
                'date': validation['date'],
                'report': '../docs/hardware-validation.json',
                'tested_version': validation['version'],
                'tested_firmware_sha256': validation['firmware_sha256'],
                'not_tested': validation['not_tested']}
(web / 'build-info.json').write_text(json.dumps(metadata, indent=2) + '\n')
print('Packaged', version, metadata['hardware_validation']['status'])
