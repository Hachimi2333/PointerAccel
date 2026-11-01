"""Run a continuous input script; expected output is frozen separately from production code."""
import json
import time


def exercise(rpc, config_path, actions, set_scale):
    base = time.monotonic_ns() // 1000
    stamp = 0
    rpc('TestReports', json.dumps({'reports': [{'device': 0, 'x': 0, 'y': 0, 'dx': 0, 'dy': 0, 'time': base}]}))
    ids = {d['name']: d['id'] for d in rpc('Devices')['devices']}
    config = {'enabled': True, 'pointer_speed': 10, 'enhance_pointer_precision': True, 'excluded_devices': [], 'trace': False}
    observations = []
    for index, action in enumerate(actions):
        kind = action['kind']
        if kind == 'config':
            config.update(action['values'])
            if 'excluded' in action:
                config['excluded_devices'] = [ids[f'pa-test-{device}'] for device in action['excluded']]
            config_path.parent.mkdir(parents=True, exist_ok=True)
            config_path.write_text(json.dumps(config))
            rpc('ApplyConfig')
        elif kind == 'temporary':
            rpc('SetTemporaryEnabled', 'true' if action['enabled'] else 'false')
        elif kind == 'reset':
            rpc('ResetState')
        elif kind == 'hotplug':
            rpc('TestHotplug')
        elif kind == 'scale':
            set_scale(action['value'])
        elif kind == 'reports':
            reports = []
            for device, x, y in action['motions']:
                stamp += 125
                reports.append({'device': device, 'x': x, 'y': y, 'dx': x * 3.125, 'dy': y * 3.125, 'time': base + stamp})
            result = rpc('TestReports', json.dumps({'reports': reports, 'reset': False, 'center': False, 'corrupt': action.get('corrupt', False)}))
            for sample in result['observed']:
                sample['time'] -= base
            observations.append({'step': index, 'reports': result['observed'],
                                 'active': result['status']['active'], 'healthy': result['status']['healthy'],
                                 'temporary_override': result['status']['temporary_override']})
        else:
            raise AssertionError(f'Unknown input action: {kind}')
    return observations
