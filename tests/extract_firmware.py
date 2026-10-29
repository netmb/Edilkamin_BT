#!/usr/bin/env python3
"""Compile actual firmware routing/response functions with host transport mocks.

No copied implementation: changes in main.cpp are exercised on every test run.
Hardware setup, AES and the timed loop remain covered by the firmware build /
subsequent device validation, not this host harness.
"""
from pathlib import Path
import sys
source = Path('src/main.cpp').read_text()

def section(start, end):
    return source[source.index(start):source.index(end, source.index(start))]

Path(sys.argv[1]).write_text('\n'.join([
    section('FanControl fans;', 'void publishFanAvailability()'),
    section('void publishFanAvailability()', 'void publishFanDiscovery()'),
    section('void publishSensor(', 'void publishSensorDiscovery()'),
    section('void publishTemperature(', 'void hexDebug('),
    section('bool queueBtCommand(', '// MQTT reconnect'),
]))
