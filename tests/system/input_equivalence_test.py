#!/usr/bin/env python3
"""Frozen e73f4c6 compositor behavior, including mixed-scale output crossing."""
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'support'))
from harness import Sandbox, test_main, wait_for

BUILD = Path(sys.argv[1]).resolve()
FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/input_equivalence.json'



class InputEquivalence(unittest.TestCase):
    def test_frozen_per_report_behavior(self):
        fixture = json.loads(FIXTURE.read_text())
        observations = []
        with Sandbox(BUILD, 'input-equivalence') as sandbox:
            kwin = Path(sandbox.env['XDG_CONFIG_HOME']) / 'kwinrc'
            kwin.write_text('[Plugins]\npointeraccel_kwinEnabled=true\n')
            config_path = kwin.parent / 'pointeraccel/config.json'
            config_path.parent.mkdir()
            config = dict(enabled=True, pointer_speed=10, enhance_pointer_precision=True,
                          excluded_devices=[], trace=True)
            config_path.write_text(json.dumps(config))
            with sandbox.compositor(BUILD / 'teststage/qt6/plugins', outputs=2):
                wait_for(lambda: sandbox.rpc('Status')['active'], 'plugin did not activate')
                sandbox.run(['kscreen-doctor', 'output.Virtual-0.scale.1', 'output.Virtual-0.position.0,0',
                             'output.Virtual-1.scale.2', 'output.Virtual-1.position.640,0'])
                # The compositor applies output changes synchronously before this D-Bus round trip.
                stamp = 1000000
                ids = {}
                for index, action in enumerate(fixture['actions']):
                    with self.subTest(step=index, action=action['kind']):
                        kind = action['kind']
                        if kind == 'config':
                            config.update(action.get('values', {}))
                            if 'excluded' in action:
                                config['excluded_devices'] = [ids[f'pa-test-{i}'] for i in action['excluded']]
                            config_path.write_text(json.dumps(config))
                            sandbox.rpc('ApplyConfig')
                        elif kind == 'temporary':
                            sandbox.rpc('SetTemporaryEnabled', str(action['enabled']).lower())
                        elif kind == 'reset':
                            sandbox.rpc('ResetState')
                        elif kind == 'hotplug':
                            sandbox.rpc('TestHotplug')
                        elif kind == 'reports':
                            reports = []
                            for device, x, y in action['motions']:
                                stamp += 125
                                reports.append(dict(device=device, x=x, y=y, dx=x * 3.125,
                                                    dy=y * 3.125, time=stamp))
                            payload = dict(reports=reports, reset=False, center=False)
                            payload.update({k: action[k] for k in ('position', 'corrupt', 'overflow') if k in action})
                            result = sandbox.rpc('TestReports', json.dumps(payload))
                            ids = {d['name']: d['id'] for d in sandbox.rpc('Devices')['devices']}
                            diagnostic = sandbox.rpc('Diagnostics')
                            observations.append(dict(step=index, reports=result['observed'],
                                positions=result['positions'],
                                status={k: result['status'][k] for k in ('active', 'healthy', 'temporary_override')},
                                trace=diagnostic.get('trace', [])))
                        else:
                            self.fail(f'Unknown action: {kind}')
        self.assertEqual(observations, fixture['observations'])


if __name__ == '__main__':
    test_main(BUILD, private_bus=True)
