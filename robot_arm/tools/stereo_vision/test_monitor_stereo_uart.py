"""Receive-only and interactive monitor tests using fake ports, never real hardware."""

from contextlib import redirect_stdout
from decimal import localcontext
import io
import json
from pathlib import Path
import queue
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

import monitor_stereo_uart as monitor


class FakePort:
    def __init__(self):
        self.incoming = queue.Queue()
        self.closed = False
        self.fail_open = False

    @property
    def in_waiting(self):
        return 1

    def open(self):
        if self.fail_open:
            raise OSError('open failed')

    def read(self, size):
        try:
            result = self.incoming.get(timeout=0.01)
        except queue.Empty:
            return b''
        if isinstance(result, Exception):
            raise result
        return result

    def write(self, data):
        raise AssertionError('Monitor must NEVER transmit')

    def close(self):
        self.closed = True


class FakeWritablePort(FakePort):
    def __init__(self, write_result=None):
        super().__init__()
        self.write_result = write_result
        self.writes = []

    def write(self, data):
        self.writes.append(data)
        if isinstance(self.write_result, BaseException):
            raise self.write_result
        return len(data) if self.write_result is None else self.write_result


class BlockingInput:
    def __init__(self):
        self.entered = threading.Event()
        self.release = threading.Event()

    def readline(self):
        self.entered.set()
        self.release.wait()
        return ''


class FakeSessionRecorder:
    def __init__(self, output, error=None):
        self.output = Path(output)
        self.error = error
        self.started = False
        self.closed = False
        self.result = {'status': 'starting'}

    def start(self):
        session = json.loads((self.output / 'session.json').read_text())
        if session['status'] != 'running':
            raise AssertionError('Recording must start after the UART session is running')
        self.started = True
        self.result = ({'status': 'error', 'error': self.error} if self.error
                       else {'status': 'recording'})

    def close(self):
        self.closed = True
        if not self.error:
            self.result = {'status': 'complete'}

    def summary(self):
        return dict(self.result)


def wait_for(predicate):
    deadline = time.monotonic() + 2.0
    while not predicate():
        if time.monotonic() >= deadline:
            raise AssertionError('Fake reader deadline expired')
        time.sleep(0.005)


class MonitorTests(unittest.TestCase):
    def test_gripper_manual_commands_require_motion_opt_in(self):
        for enabled in (False, True):
            with self.subTest(enabled=enabled), tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
                logs = monitor.MonitorLogs(Path(folder))
                ports = [FakeWritablePort(), FakeWritablePort()]
                instance = monitor.ConsoleMonitor(ports, logs, True, enabled)
                try:
                    instance.handle_input('r O')
                    instance.handle_input('r H')
                finally:
                    instance.close()
                    logs.close()
                self.assertEqual(ports[0].writes, [])
                self.assertEqual(ports[1].writes, [b'O', b'H'] if enabled else [])
                self.assertEqual([event['status'] for event in logs.commands],
                                 ['sent', 'sent'] if enabled else ['blocked', 'blocked'])

    def test_gripper_motion_trace_is_saved_without_command_console_flood(self):
        with tempfile.TemporaryDirectory() as folder:
            logs = monitor.MonitorLogs(Path(folder), 'commands')
            logs.arm_response('right', 'O')
            logs.feed('right', b'GM,1,2,300,1,1,1,12.00,0,0\n')
            logs.feed('right', b'GO,1,2,300,1,4,300\n')
            self.assertTrue(logs.display.empty())
            logs.close()
            self.assertIn('GM,1,2,300', (Path(folder) / 'combined_uart.log').read_text())
            self.assertIn('GO,1,2,300', (Path(folder) / 'combined_uart.log').read_text())

    def test_motion_stationarity_trace_is_hidden_but_saved_in_commands_mode(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)
            logs = monitor.MonitorLogs(output, 'commands')
            logs.feed('right', b'MS,515,100,1,6,2.500,3.000,0.000,0\n')
            self.assertTrue(logs.display.empty())
            logs.close()
            self.assertIn('MS,515,100,1,6', (output / 'combined_uart.log').read_text())

    def test_fragmented_lines_and_exact_raw(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)
            logs = monitor.MonitorLogs(output)
            raw = 'boot\r\n[ST] 한글\r\n'.encode('utf-8') + b'bad\xff\npartial'
            for position in range(0, len(raw), 2):
                logs.feed('left', raw[position:position + 2])
            logs.feed('right', b'[ST] rx=3\r\n')
            logs.close()
            self.assertEqual((output / 'left_uart.raw').read_bytes(), raw)
            left = (output / 'left_uart.log').read_text(encoding='utf-8')
            self.assertIn('[ST] 한글\n', left)
            self.assertIn('bad\ufffd\n', left)
            self.assertIn('partial [HOST: partial line at stop]', left)
            combined = (output / 'combined_uart.log').read_text(encoding='utf-8')
            self.assertIn('[L] boot', combined)
            self.assertIn('[R] [ST] rx=3', combined)

    def test_screen_filter_keeps_full_disk_logs(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)
            logs = monitor.MonitorLogs(output, 'stereo')
            raw = b'boot\n[ST] tx=4\nCNN done\n'
            logs.feed('left', raw)
            logs.close()
            self.assertEqual(logs.display.qsize(), 1)
            self.assertIn('[L] [ST] tx=4', logs.display.get_nowait())
            self.assertEqual((output / 'left_uart.raw').read_bytes(), raw)
            self.assertIn('CNN done', (output / 'combined_uart.log').read_text())

    def test_console_filter_keeps_menus_errors_and_full_logs(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)
            logs = monitor.MonitorLogs(output, 'console')
            raw = (b'TK,1,2\n#CN,fid,t_ms\nCN,1,2\n'
                   b'CNN RGBY detection settings\n'
                   b'  green margin [current 50, 0..255, Enter=keep]: TK,3,4\n'
                   b'  RGBY settings applied (0)\nCE,1,2,-1\nEV,1,2\n[ST] rx=4\n')
            logs.feed('right', raw)
            shown = [logs.display.get_nowait() for entry in range(logs.display.qsize())]
            self.assertEqual(len(shown), 6)
            self.assertTrue(any('green margin' in entry for entry in shown))
            self.assertTrue(any('settings applied' in entry for entry in shown))
            self.assertTrue(any('CE,1,2,-1' in entry for entry in shown))
            self.assertTrue(any('EV,1,2' in entry for entry in shown))
            self.assertFalse(any('[R] TK,' in entry or '[R] CN,' in entry for entry in shown))
            logs.close()
            self.assertEqual((output / 'right_uart.raw').read_bytes(), raw)
            self.assertEqual((output / 'right_uart.log').read_text(), raw.decode())

    def test_stereo_prompt_coalesced_with_trace_is_not_hidden(self):
        with tempfile.TemporaryDirectory() as folder:
            logs = monitor.MonitorLogs(Path(folder), 'stereo')
            prompt = b'  green margin [current 50, 0..255, Enter=keep]: '
            logs.feed('right', prompt + b'TK,1,2\n')
            self.assertEqual(logs.display.qsize(), 1)
            self.assertIn(prompt.decode(), logs.display.get_nowait())
            logs.close()

    def test_slow_display_does_not_drop_disk_logs(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)
            logs = monitor.MonitorLogs(output, queue_size=1)
            raw = b'one\ntwo\nthree\n'
            logs.feed('right', raw)
            logs.close()
            self.assertEqual(logs.stats['right']['display_dropped'], 2)
            self.assertEqual((output / 'right_uart.raw').read_bytes(), raw)
            self.assertEqual((output / 'right_uart.log').read_text(), raw.decode())
            self.assertEqual(len((output / 'combined_uart.log').read_text().splitlines()), 3)

    def test_unterminated_line_is_bounded_and_raw_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)
            logs = monitor.MonitorLogs(output)
            raw = b'x' * (monitor.MAX_LINE_BYTES * 2 + 7)
            logs.feed('left', raw)
            self.assertLess(len(logs.buffers['left']), monitor.MAX_LINE_BYTES)
            self.assertEqual(logs.stats['left']['split_lines'], 2)
            logs.close()
            self.assertEqual((output / 'left_uart.raw').read_bytes(), raw)

    def test_both_readers_and_disconnect_stop(self):
        with tempfile.TemporaryDirectory() as folder:
            logs = monitor.MonitorLogs(Path(folder))
            ports = [FakePort(), FakePort()]
            instance = monitor.ConsoleMonitor(ports, logs)
            try:
                instance.start()
                for port in ports:
                    port.incoming.put(b'[ST] okay\n')
                wait_for(lambda: all(stats['lines'] == 1 for stats in logs.stats.values()))
                ports[1].incoming.put(OSError('disconnected'))
                wait_for(instance.stop.is_set)
            finally:
                instance.close()
                logs.close()
            self.assertIn('[R] OSError: disconnected', instance.errors.get_nowait())
            self.assertTrue(all(not thread.is_alive() for thread in instance.threads))

    def test_open_port_configures_receive_only_console(self):
        port = FakePort()
        with patch('serial.Serial', return_value=port) as constructor:
            import serial
            self.assertIs(monitor.open_port(serial, 'COM3', 921600), port)
        self.assertFalse(port.dtr)
        self.assertFalse(port.rts)
        self.assertEqual(port.port, 'COM3')
        self.assertEqual(constructor.call_args.kwargs['timeout'], 0.1)
        self.assertEqual(constructor.call_args.kwargs['write_timeout'], 0.5)
        self.assertFalse(constructor.call_args.kwargs['rtscts'])

    def test_open_failure_closes_port(self):
        port = FakePort()
        port.fail_open = True
        with patch('serial.Serial', return_value=port):
            import serial
            with self.assertRaises(OSError):
                monitor.open_port(serial, 'COM3', 921600)
        self.assertTrue(port.closed)

    def run_main(self, folder, open_effect, display_effect=None, extra_args=(), stdin=None,
                 obs_factory=FakeSessionRecorder, review_factory=None):
        args = ['monitor', '--left', 'COM3', '--right', 'COM4',
                '--output', str(Path(folder) / 'session'), *extra_args]
        with patch('sys.argv', args), patch.object(monitor, 'open_port',
                side_effect=open_effect), patch.object(monitor, 'display_logs',
                side_effect=display_effect), patch.object(monitor, 'create_obs_recorder',
                side_effect=obs_factory), patch('sys.stdin', stdin or io.StringIO()), \
                patch.object(monitor, 'create_xyz_review', side_effect=review_factory,
                             return_value={'status': 'skipped', 'reason': 'No PAIR rows received'}), \
                redirect_stdout(io.StringIO()):
            return monitor.main()

    def test_review_runs_after_ports_logs_and_video_finalize(self):
        with tempfile.TemporaryDirectory() as folder:
            ports = [FakeWritablePort(), FakeWritablePort()]
            video = FakeSessionRecorder(Path(folder) / 'session')
            calls = []

            def review(output, open_browser):
                self.assertTrue(all(port.closed for port in ports))
                self.assertTrue(video.closed)
                self.assertEqual(json.loads((output / 'session.json').read_text())['status'], 'stopped')
                self.assertTrue((output / 'combined_uart.log').is_file())
                calls.append(open_browser)
                return {'status': 'complete', 'path': 'review.html', 'browser_opened': False}

            self.assertEqual(self.run_main(folder, ports, obs_factory=lambda output: video,
                                           review_factory=review), 0)
            self.assertEqual(calls, [True])

    def test_review_failure_does_not_fail_uart_session(self):
        with tempfile.TemporaryDirectory() as folder:
            def fail(output, open_browser):
                raise RuntimeError('viewer unavailable')

            self.assertEqual(self.run_main(folder, [FakePort(), FakePort()], review_factory=fail), 0)
            session = json.loads((Path(folder) / 'session/session.json').read_text())
            self.assertEqual(session['status'], 'stopped')
            self.assertEqual(session['xyz_review']['status'], 'error')

    def test_no_review_never_generates_or_opens_viewer(self):
        with tempfile.TemporaryDirectory() as folder:
            def unexpected(output, open_browser):
                raise AssertionError('Review must not run')

            self.assertEqual(self.run_main(folder, [FakePort(), FakePort()],
                                           extra_args=['--no-review'], review_factory=unexpected), 0)
            session = json.loads((Path(folder) / 'session/session.json').read_text())
            self.assertEqual(session['xyz_review']['status'], 'disabled')

    def test_no_video_still_generates_review_without_browser_when_requested(self):
        with tempfile.TemporaryDirectory() as folder:
            calls = []

            def review(output, open_browser):
                calls.append(open_browser)
                return {'status': 'skipped', 'reason': 'No PAIR rows received'}

            self.assertEqual(self.run_main(folder, [FakePort(), FakePort()],
                                           extra_args=['--no-video', '--no-open-review'],
                                           review_factory=review), 0)
            self.assertEqual(calls, [False])

    def test_real_review_helper_skips_empty_log_without_browser(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)
            (output / 'combined_uart.log').write_text('', encoding='utf-8')
            with patch.object(monitor.webbrowser, 'open') as browser:
                result = monitor.create_xyz_review(output)
            self.assertEqual(result['status'], 'skipped')
            self.assertFalse((output / 'xyz_review').exists())
            self.assertTrue((output / 'xyz_review_build.log').is_file())
            browser.assert_not_called()

    def test_real_review_helper_generates_recorded_angles_and_opens_one_page(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)
            payload = ('PAIR,1000,40,1,10,20,700,30,9,0,10000,-2,0,0,0,0,0\n'
                       'PG,40,1,1,OK\n'
                       'A1,700,1000,5,300,1,63,1,1,-20,30,40,50,0.5\n')
            (output / 'combined_uart.log').write_text('\n'.join(
                f'2026-10-05T01:00:00.000+00:00 [R] {line}'
                for line in payload.splitlines()), encoding='utf-8')
            with patch.object(monitor.webbrowser, 'open', return_value=True) as browser:
                result = monitor.create_xyz_review(output)
            self.assertEqual(result['status'], 'complete')
            self.assertEqual(result['angle_pairs'], 1)
            browser.assert_called_once_with((output / 'xyz_review/review.html').as_uri())
            data = json.loads((output / 'xyz_review/review.json').read_text(encoding='utf-8'))
            self.assertEqual(data['frames'][0]['A1']['angles'], [-20, 30, 40, 50, 0.5])

    def test_default_command_records_video_and_finalizes_on_local_quit(self):
        with tempfile.TemporaryDirectory() as folder:
            ports = [FakeWritablePort(), FakeWritablePort()]
            video = FakeSessionRecorder(Path(folder) / 'session')
            real_display = monitor.display_logs

            def display(instance):
                self.assertTrue(video.started)
                self.assertFalse(video.closed)
                real_display(instance)

            result = self.run_main(folder, ports, display,
                                   ['--interactive', '--allow-motion-commands',
                                    '--filter', 'commands', '--format', 'plain'],
                                   io.StringIO('q\n'), lambda output: video)
            self.assertEqual(result, 0)
            self.assertTrue(video.closed)
            self.assertEqual([port.writes for port in ports], [[], []])
            session = json.loads((Path(folder) / 'session/session.json').read_text())
            self.assertEqual(session['obs']['status'], 'complete')
            self.assertEqual(session['status'], 'stopped')

    def test_ctrl_c_finalizes_video_without_board_commands(self):
        with tempfile.TemporaryDirectory() as folder:
            ports = [FakeWritablePort(), FakeWritablePort()]
            video = FakeSessionRecorder(Path(folder) / 'session')

            def interrupt(instance):
                raise KeyboardInterrupt

            self.assertEqual(self.run_main(folder, ports, interrupt,
                                           obs_factory=lambda output: video), 130)
            self.assertTrue(video.closed)
            self.assertEqual([port.writes for port in ports], [[], []])
            session = json.loads((Path(folder) / 'session/session.json').read_text())
            self.assertEqual(session['obs']['status'], 'complete')
            self.assertEqual(session['status'], 'interrupted')

    def test_obs_failure_keeps_uart_monitor_and_persists_warning(self):
        with tempfile.TemporaryDirectory() as folder:
            ports = [FakeWritablePort(), FakeWritablePort()]
            video = FakeSessionRecorder(Path(folder) / 'session', 'OBS unavailable')
            self.assertEqual(self.run_main(folder, ports, monitor.display_logs,
                                           ['--interactive'], io.StringIO('r ?\nq\n'),
                                           lambda output: video), 0)
            self.assertEqual([port.writes for port in ports], [[], [b'?']])
            self.assertTrue(video.closed)
            session = json.loads((Path(folder) / 'session/session.json').read_text())
            self.assertEqual(session['obs'], {'status': 'error', 'error': 'OBS unavailable'})
            self.assertEqual(session['errors'], [])

    def test_missing_recorder_keeps_uart_logging(self):
        with tempfile.TemporaryDirectory() as folder:
            ports = [FakePort(), FakePort()]

            def missing(output):
                raise ImportError('recorder missing')

            self.assertEqual(self.run_main(folder, ports, obs_factory=missing), 0)
            session = json.loads((Path(folder) / 'session/session.json').read_text())
            self.assertEqual(session['obs']['status'], 'error')
            self.assertIn('recorder missing', session['obs']['error'])

    def test_no_video_does_not_load_obs(self):
        with tempfile.TemporaryDirectory() as folder:
            ports = [FakePort(), FakePort()]

            def unexpected(output):
                raise AssertionError('OBS must not load with --no-video')

            self.assertEqual(self.run_main(folder, ports, extra_args=['--no-video'],
                                           obs_factory=unexpected), 0)
            session = json.loads((Path(folder) / 'session/session.json').read_text())
            self.assertEqual(session['obs'], {'status': 'disabled'})

    def test_uart_open_failure_does_not_start_recording(self):
        with tempfile.TemporaryDirectory() as folder:
            left = FakePort()

            def unexpected(output):
                raise AssertionError('OBS must not load before both UART ports open')

            self.assertEqual(self.run_main(folder, [left, OSError('right busy')],
                                           obs_factory=unexpected), 1)
            self.assertTrue(left.closed)

    def test_second_port_failure_closes_first_and_records_error(self):
        with tempfile.TemporaryDirectory() as folder:
            left = FakePort()
            self.assertEqual(self.run_main(folder, [left, OSError('right busy')]), 1)
            self.assertTrue(left.closed)
            session = json.loads((Path(folder) / 'session/session.json').read_text())
            self.assertEqual(session['status'], 'error')
            self.assertIn('right busy', session['errors'][0])

    def test_ctrl_c_closes_both_ports_and_flushes_logs(self):
        with tempfile.TemporaryDirectory() as folder:
            ports = [FakePort(), FakePort()]
            for port in ports:
                port.incoming.put(b'[ST] ready\n')

            def interrupt(instance):
                wait_for(lambda: all(stats['lines'] == 1
                                     for stats in instance.logs.stats.values()))
                raise KeyboardInterrupt

            self.assertEqual(self.run_main(folder, ports, interrupt), 130)
            self.assertTrue(all(port.closed for port in ports))
            session = json.loads((Path(folder) / 'session/session.json').read_text())
            self.assertEqual(session['status'], 'interrupted')
            self.assertTrue(session['receive_only'])
            self.assertEqual(session['stats']['left']['lines'], 1)

    def test_existing_session_never_overwritten(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / 'session'
            output.mkdir()
            sentinel = output / 'combined_uart.log'
            sentinel.write_text('keep')
            with self.assertRaises(FileExistsError):
                self.run_main(folder, [])
            self.assertEqual(sentinel.read_text(), 'keep')

    def test_disconnect_exit_code_and_manifest(self):
        with tempfile.TemporaryDirectory() as folder:
            ports = [FakePort(), FakePort()]
            ports[1].incoming.put(OSError('unplugged'))

            def wait_disconnect(instance):
                wait_for(instance.stop.is_set)

            self.assertEqual(self.run_main(folder, ports, wait_disconnect), 1)
            self.assertTrue(all(port.closed for port in ports))
            session = json.loads((Path(folder) / 'session/session.json').read_text())
            self.assertEqual(session['status'], 'error')
            self.assertIn('[R] OSError: unplugged', session['errors'][0])

    def test_parser_targets_case_and_invalid_input(self):
        for text, expected in [('l ?\n', ('l', '?')), ('r C\r\n', ('r', 'C')),
                               ('both q', ('both', 'q')), ('r p', ('r', 'p')),
                               ('q\n', ('exit', None))]:
            with self.subTest(text=text):
                self.assertEqual(monitor.parse_command(text), expected)
        for text in ['', '\n', ' \t \r\n']:
            self.assertIsNone(monitor.parse_command(text))
        for text in ['?', 'l', 'left ?', 'L ?', 'l ??', 'l ? extra', 'both',
                     'l 한', '한 ?', 'l \x00', 'l \x1b', 'l ?\nr ?', 'l\u00a0?']:
            with self.subTest(text=text), self.assertRaises(ValueError):
                monitor.parse_command(text)

    def test_targeted_tx_exact_bytes_and_rx_separation(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()) as screen:
            output = Path(folder)
            logs = monitor.MonitorLogs(output, 'stereo')
            ports = [FakeWritablePort(), FakeWritablePort()]
            instance = monitor.ConsoleMonitor(ports, logs, interactive=True)
            try:
                instance.handle_input('l ?\n')
                instance.handle_input('r C\n')
                instance.handle_input('both q\n')
                instance.handle_input('r c\n')
                logs.feed('left', b'menu reply\r\n')
                logs.feed('right', b'[ST] rx=1\n')
            finally:
                instance.close()
                logs.close()
            self.assertEqual(ports[0].writes, [b'?', b'q'])
            self.assertEqual(ports[1].writes, [b'C', b'q', b'c'])
            self.assertEqual((output / 'left_uart.raw').read_bytes(), b'menu reply\r\n')
            self.assertEqual((output / 'right_uart.raw').read_bytes(), b'[ST] rx=1\n')
            self.assertNotIn('[HOST]', (output / 'combined_uart.log').read_text())
            audit = [json.loads(line) for line in
                     (output / 'host_commands.jsonl').read_text().splitlines()]
            self.assertEqual(audit, logs.commands)
            self.assertEqual(audit[2]['target'], 'both')
            self.assertEqual([result['bytes_written'] for result in audit[2]['results']], [1, 1])
            self.assertIn('[HOST] command', screen.getvalue())
            self.assertIn('"command": "C"', screen.getvalue())
            self.assertEqual(logs.display.qsize(), 1)

    def test_decimal_submenu_parser_and_rejected_payloads(self):
        for text, expected in [('r value 30', ('r', '30\r')),
                               ('l value 000001\r\n', ('l', '000001\r')),
                               ('r value 0', ('r', '0\r')),
                               ('l enter', ('l', '\r')),
                               ('both value 40', ('both', '40\r')),
                               ('both value 000100\r\n', ('both', '000100\r')),
                               ('both value 0', ('both', '0\r')),
                               ('both enter\n', ('both', '\r')),
                               ('both\tvalue\t262143', ('both', '262143\r'))]:
            with self.subTest(text=text):
                self.assertEqual(monitor.parse_command(text), expected)
        for text in ['r value', 'r value 1234567',
                     'r value -1', 'r value +1', 'r value 3.0', 'r value 0x10',
                     'r value E', 'r value X', 'r value j', 'r value 3 E',
                     'r value 3\rE', 'r value ３０', 'r enter E', 'r raw E',
                     'BOTH value 40', 'Both enter', 'both VALUE 40', 'both ENTER',
                     'both value', 'both value 1234567', 'both value -1',
                     'both value +1', 'both value 3.0', 'both value 0x10',
                     'both value E', 'both value 3 E', 'both value 3\rE',
                     'both value ４０', 'both\u00a0value 40', 'both enter E']:
            with self.subTest(text=text), self.assertRaises(ValueError):
                monitor.parse_command(text)

    def test_filter_parser_default_wire_contract(self):
        for text, payload in [
                ('r filter show', '~F,SHOW\r'),
                ('r filter default\r\n', '~F,DEFAULT\r'),
                ('r filter 2d ema tau 0.10', '~F,EMA,100000\r'),
                ('r filter 3d min 0.5', '~F,MIN,500\r'),
                ('r filter 3d beta 0.001', '~F,BETA,1000\r'),
                ('r\tfilter\t3d\tderivative\t1', '~F,DERIVATIVE,1000\r')]:
            with self.subTest(text=text):
                self.assertEqual(monitor.parse_command(text), ('r', payload))

    def test_filter_parser_inclusive_bounds_and_exact_scaling(self):
        for setting, values, operation in [
                ('2d ema tau', [('0.001', 1000), ('1', 1000000),
                                ('0.030001', 30001), ('000.1000000', 100000)], 'EMA'),
                ('3d min', [('0.01', 10), ('10', 10000), ('0.501', 501)], 'MIN'),
                ('3d beta', [('0', 0), ('0.0000000', 0), ('0.1', 100000),
                             ('0.000001', 1), ('0.012345', 12345)], 'BETA'),
                ('3d derivative', [('0.01', 10), ('10', 10000),
                                   ('1.001000', 1001)], 'DERIVATIVE')]:
            for token, scaled in values:
                with self.subTest(setting=setting, token=token):
                    self.assertEqual(monitor.parse_command(f'r filter {setting} {token}'),
                                     ('r', f'~F,{operation},{scaled}\r'))

    def test_filter_parser_rejects_non_plain_decimals(self):
        for setting in ['2d ema tau', '3d min', '3d beta', '3d derivative']:
            for token in ['NaN', 'sNaN', 'inf', 'Infinity', '-0', '-1', '+0.1',
                          '1e-2', '1E-2', '0x1', '.1', '1.', '0,1', '0_1',
                          '０.１', '1/10', '1 2', '0.1\rE', '0.1\n~F,SHOW']:
                with self.subTest(setting=setting, token=token), self.assertRaises(ValueError):
                    monitor.parse_command(f'r filter {setting} {token}')

    def test_filter_parser_rejects_ranges_and_fractional_wire_units(self):
        for setting, tokens in [
                ('2d ema tau', ['0', '0.000999', '1.000001', '0.1000001']),
                ('3d min', ['0', '0.009', '10.001', '0.0101', '0.5000001']),
                ('3d beta', ['0.100001', '0.0000001', '0.00100001']),
                ('3d derivative', ['0', '0.009', '10.001', '1.0001'])]:
            for token in tokens:
                with self.subTest(setting=setting, token=token), self.assertRaises(ValueError):
                    monitor.parse_command(f'r filter {setting} {token}')

    def test_filter_parser_preserves_precision_beyond_decimal_context(self):
        with localcontext() as context:
            context.prec = 3
            for text, payload in [
                    ('r filter 2d ema tau 0.123456', '~F,EMA,123456\r'),
                    ('r filter 2d ema tau 0.1000000000000000000000000000000000000',
                     '~F,EMA,100000\r'),
                    ('r filter 3d beta 0.0123450000000000000000000000000000000',
                     '~F,BETA,12345\r')]:
                with self.subTest(text=text):
                    self.assertEqual(monitor.parse_command(text), ('r', payload))
            for setting, token in [
                    ('2d ema tau', '0.1000000000000000000000000000000000001'),
                    ('3d min', '0.5000000000000000000000000000000000001'),
                    ('3d beta', '0.0010000000000000000000000000000000001'),
                    ('3d derivative', '1.0000000000000000000000000000000000001'),
                    ('2d ema tau', '1.0000000000000000000000000000000000001'),
                    ('3d min', '0.0099999999999999999999999999999999999'),
                    ('3d beta', '0.1000000000000000000000000000000000001')]:
                with self.subTest(setting=setting, token=token), self.assertRaises(ValueError):
                    monitor.parse_command(f'r filter {setting} {token}')

    def test_filter_parser_rejects_other_targets_syntax_and_raw_payloads(self):
        for target in ['l', 'both', 'R', 'right']:
            for setting in ['show', 'default', '2d ema tau 0.1', '3d min 0.5',
                            '3d beta 0.001', '3d derivative 1']:
                with self.subTest(target=target, setting=setting), self.assertRaises(ValueError):
                    monitor.parse_command(f'{target} filter {setting}')
        for text in ['r filter', 'r filter SHOW', 'r Filter show', 'r filter show extra',
                     'r filter default 0', 'r filter 2d ema tau', 'r filter 3d min',
                     'r filter 2d tau 0.1', 'r filter 2d ema beta 0.1',
                     'r filter 3d ema 0.1', 'r filter 3d MIN 0.5',
                     'r filter 3d min 0.5 extra', 'r filter ~F,SHOW',
                     'r ~F,SHOW', 'r ~F,EMA,100000\r', 'r raw ~F,DEFAULT',
                     'r value ~F,SHOW', 'r filter show\nr E']:
            with self.subTest(text=text), self.assertRaises(ValueError):
                monitor.parse_command(text)

    def test_filter_commands_exact_right_tx_and_full_audit(self):
        commands = ['r filter show', 'r filter default', 'r filter 2d ema tau 0.1',
                    'r filter 3d min 0.5', 'r filter 3d beta 0.001',
                    'r filter 3d derivative 1']
        payloads = [b'~F,SHOW\r', b'~F,DEFAULT\r', b'~F,EMA,100000\r',
                    b'~F,MIN,500\r', b'~F,BETA,1000\r', b'~F,DERIVATIVE,1000\r']
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
            output = Path(folder)
            logs = monitor.MonitorLogs(output, 'commands')
            ports = [FakeWritablePort(), FakeWritablePort()]
            instance = monitor.ConsoleMonitor(ports, logs, True)
            try:
                for command in commands:
                    instance.handle_input(command)
                self.assertEqual(ports[0].writes, [])
                self.assertEqual(ports[1].writes, payloads)
                self.assertIsNone(logs.response_windows['left'])
                for event, command, payload in zip(logs.commands, commands, payloads):
                    self.assertEqual(event['input'], command)
                    self.assertEqual(event['command'], payload.decode('ascii'))
                    self.assertEqual(event['target'], 'r')
                    self.assertEqual(event['status'], 'sent')
                    self.assertEqual(event['results'], [{'side': 'right',
                        'bytes_requested': len(payload), 'bytes_written': len(payload),
                        'status': 'sent'}])
            finally:
                instance.close()
                logs.close()
            audit = [json.loads(line) for line in
                     (output / 'host_commands.jsonl').read_text().splitlines()]
            self.assertEqual(audit, logs.commands)

    def test_invalid_and_receive_only_filter_commands_never_transmit(self):
        for interactive in [False, True]:
            with self.subTest(interactive=interactive), tempfile.TemporaryDirectory() as folder, \
                    redirect_stdout(io.StringIO()):
                logs = monitor.MonitorLogs(Path(folder), 'commands')
                instance = monitor.ConsoleMonitor([FakePort(), FakePort()], logs, interactive)
                try:
                    commands = (['l filter show', 'both filter default', 'r filter 3d beta NaN',
                                 'r filter 2d ema tau 0.1000001', 'r ~F,SHOW'] if interactive else
                                ['r filter default', 'r filter 2d ema tau 0.1'])
                    for command in commands:
                        instance.handle_input(command)
                    self.assertEqual(len(logs.commands), len(commands) if interactive else 0)
                    self.assertTrue(all(event['status'] == 'rejected' for event in logs.commands))
                    self.assertTrue(all(event['results'] == [] for event in logs.commands))
                    self.assertEqual(logs.response_windows, {'left': None, 'right': None})
                    self.assertFalse(instance.stop.is_set())
                finally:
                    instance.close()
                    logs.close()

    def test_filter_commands_short_or_failed_write_stop_without_retry(self):
        for result in [0, 5, OSError('unplugged')]:
            with self.subTest(result=result), tempfile.TemporaryDirectory() as folder, \
                    redirect_stdout(io.StringIO()):
                logs = monitor.MonitorLogs(Path(folder), 'commands')
                ports = [FakeWritablePort(), FakeWritablePort(result)]
                instance = monitor.ConsoleMonitor(ports, logs, True)
                try:
                    instance.handle_input('r filter 2d ema tau 0.1')
                    instance.handle_input('r filter default')
                    self.assertEqual(ports[0].writes, [])
                    self.assertEqual(ports[1].writes, [b'~F,EMA,100000\r'])
                    self.assertEqual(logs.commands[0]['status'], 'failed')
                    self.assertEqual(logs.commands[0]['results'][0]['bytes_written'],
                                     None if isinstance(result, Exception) else result)
                    self.assertIsNone(logs.response_windows['right'])
                    self.assertTrue(instance.stop.is_set())
                finally:
                    instance.close()
                    logs.close()

    def test_filter_ack_and_status_visible_in_commands_window_and_saved(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()), \
                patch.object(monitor.time, 'monotonic', return_value=100.0) as clock:
            output = Path(folder)
            logs = monitor.MonitorLogs(output, 'commands', display_format='plain')
            reply = b'ACK,F,SHOW,OK\r\nFILTER,EMA,100000,MIN,500,BETA,1000,DERIVATIVE,1000\r\n'
            telemetry = b'TK,1\nRQ,1,2\n[ST] tx=3\n'

            class ImmediateFilterReply(FakeWritablePort):
                def write(self, data):
                    self.writes.append(data)
                    logs.feed('left', reply)
                    for byte in reply:
                        logs.feed('right', bytes([byte]))
                    logs.feed('right', telemetry)
                    return len(data)

            instance = monitor.ConsoleMonitor([FakePort(), ImmediateFilterReply()], logs, True)
            try:
                logs.feed('right', b'FILTER,unsolicited\n')
                instance.handle_input('r filter show')
                self.assertEqual(logs.display.get_nowait(), '[R] ACK,F,SHOW,OK')
                self.assertEqual(logs.display.get_nowait(),
                                 '[R] FILTER,EMA,100000,MIN,500,BETA,1000,DERIVATIVE,1000')
                self.assertEqual(logs.display.qsize(), 0)
                clock.return_value = 104.0
                logs.feed('right', b'ACK,F,late\n')
                self.assertEqual(logs.display.qsize(), 0)
            finally:
                instance.close()
                logs.close()
            raw = b'FILTER,unsolicited\n' + reply + telemetry + b'ACK,F,late\n'
            self.assertEqual((output / 'right_uart.raw').read_bytes(), raw)
            self.assertEqual((output / 'left_uart.raw').read_bytes(), reply)
            combined = (output / 'combined_uart.log').read_text()
            self.assertIn('[R] ACK,F,SHOW,OK', combined)
            self.assertIn('[R] FILTER,EMA,100000', combined)
            self.assertIn('[R] ACK,F,late', combined)

    def test_filter_main_manifest_and_no_automatic_tx(self):
        with tempfile.TemporaryDirectory() as folder:
            ports = [FakeWritablePort(), FakeWritablePort()]
            result = self.run_main(folder, ports, monitor.display_logs,
                                   ['--interactive', '--filter', 'commands', '--format', 'plain'],
                                   io.StringIO('r filter show\nr filter default\nq\n'))
            self.assertEqual(result, 0)
            self.assertEqual(ports[0].writes, [])
            self.assertEqual(ports[1].writes, [b'~F,SHOW\r', b'~F,DEFAULT\r'])
            self.assertTrue(all(port.closed for port in ports))
            session = json.loads((Path(folder) / 'session/session.json').read_text())
            self.assertFalse(session['allow_motion_commands'])
            self.assertEqual([event['command'] for event in session['commands']],
                             ['~F,SHOW\r', '~F,DEFAULT\r', None])

    def test_margin_menu_thirteen_fields_and_audit(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
            output = Path(folder)
            logs = monitor.MonitorLogs(output)
            ports = [FakeWritablePort(), FakeWritablePort()]
            instance = monitor.ConsoleMonitor(ports, logs, True)
            try:
                instance.handle_input('r m')
                instance.handle_input('r value 30')
                for field in range(12):
                    instance.handle_input('r enter')
                self.assertEqual(len(ports[1].writes), 14)
                instance.handle_input('r a')
            finally:
                instance.close()
                logs.close()
            self.assertEqual(ports[0].writes, [])
            self.assertEqual(ports[1].writes, [b'm', b'30\r', *([b'\r'] * 12), b'a'])
            self.assertEqual(logs.commands[1]['command'], '30\r')
            self.assertEqual(logs.commands[1]['results'][0]['bytes_requested'], 3)
            self.assertEqual(logs.commands[1]['results'][0]['bytes_written'], 3)
            self.assertEqual(logs.commands[2]['results'][0]['bytes_requested'], 1)
            audit = [json.loads(line) for line in
                     (output / 'host_commands.jsonl').read_text().splitlines()]
            self.assertEqual(audit, logs.commands)
            self.assertEqual((output / 'right_uart.raw').read_bytes(), b'')
            self.assertEqual((output / 'combined_uart.log').read_bytes(), b'')

    def test_numeric_input_read_only_rejections_and_guards(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
            logs = monitor.MonitorLogs(Path(folder))
            instance = monitor.ConsoleMonitor([FakePort(), FakePort()], logs)
            try:
                instance.handle_input('r value 30')
                instance.handle_input('r enter')
                instance.handle_input('both value 40')
                instance.handle_input('both enter')
                self.assertEqual(logs.commands, [])
                instance.interactive = True
                for text in ['r value E', 'r value 1234567', 'both enter E', 'r enter E']:
                    instance.handle_input(text)
                for command in ['E', 'X', 'A', 'R', 'P', 'j']:
                    instance.handle_input('r ' + command)
                self.assertEqual([event['status'] for event in logs.commands],
                                 ['rejected'] * 4 + ['blocked'] * 6)
            finally:
                instance.close()
                logs.close()

    def test_both_margin_menu_exact_bytes_warnings_and_audit(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()) as screen:
            output = Path(folder)
            logs = monitor.MonitorLogs(output, display_format='plain')
            ports = [FakeWritablePort(), FakeWritablePort()]
            instance = monitor.ConsoleMonitor(ports, logs, True)
            try:
                instance.handle_input('both m')
                for field in range(13):
                    instance.handle_input('both value 40' if field == 0 else
                                          'both value 100' if field == 5 else 'both enter')
            finally:
                instance.close()
                logs.close()
            expected = [b'm', b'40\r', *([b'\r'] * 4), b'100\r', *([b'\r'] * 7)]
            self.assertEqual(ports[0].writes, expected)
            self.assertEqual(ports[1].writes, expected)
            self.assertEqual(len(logs.commands), 14)
            for event in logs.commands:
                self.assertEqual(event['status'], 'sent')
                self.assertEqual([result['side'] for result in event['results']],
                                 ['left', 'right'])
                self.assertTrue(all(result['bytes_written'] == result['bytes_requested']
                                    for result in event['results']))
            self.assertIn('BOTH submenu commands require BOTH boards at the same menu field',
                          screen.getvalue())
            self.assertIn('sequential, not atomic; no automatic retry', screen.getvalue())
            self.assertIn('[HOST] both value 40: sent (L 3/3 sent, R 3/3 sent)',
                          screen.getvalue())
            self.assertIn('[HOST] both enter: sent (L 1/1 sent, R 1/1 sent)',
                          screen.getvalue())
            audit = [json.loads(line) for line in
                     (output / 'host_commands.jsonl').read_text().splitlines()]
            self.assertEqual(audit, logs.commands)
            self.assertEqual((output / 'combined_uart.log').read_bytes(), b'')

    def test_both_submenu_partial_sends_stop_without_retry(self):
        for target_index in range(2):
            for command, payload in [('both value 100', b'100\r'), ('both enter', b'\r')]:
                for failure in [0, len(payload) - 1, OSError('TX failed')]:
                    with self.subTest(target_index=target_index, command=command,
                                      failure=failure), tempfile.TemporaryDirectory() as folder, \
                            redirect_stdout(io.StringIO()) as screen:
                        logs = monitor.MonitorLogs(Path(folder), 'commands', display_format='plain')
                        ports = [FakeWritablePort(), FakeWritablePort()]
                        ports[target_index].write_result = failure
                        instance = monitor.ConsoleMonitor(ports, logs, True)
                        try:
                            instance.handle_input(command)
                            instance.handle_input('both enter')
                            self.assertTrue(instance.stop.is_set())
                            self.assertEqual(len(logs.commands), 1)
                            event = logs.commands[0]
                            self.assertEqual(event['status'], 'failed' if target_index == 0 else
                                             'partial')
                            failed = event['results'][target_index]
                            self.assertEqual(failed['status'], 'failed')
                            self.assertEqual(failed['bytes_requested'], len(payload))
                            self.assertEqual(failed['bytes_written'], None if isinstance(
                                failure, Exception) else failure)
                            self.assertEqual(ports[target_index].writes, [payload])
                            if target_index == 0:
                                self.assertEqual(event['results'][1]['status'], 'not_attempted')
                                self.assertEqual(ports[1].writes, [])
                                self.assertIn('R 0/' + str(len(payload)) + ' not_attempted',
                                              screen.getvalue())
                            else:
                                self.assertEqual(event['results'][0]['status'], 'sent')
                                self.assertEqual(ports[0].writes, [payload])
                                self.assertIn('L ' + str(len(payload)) + '/' + str(len(payload))
                                              + ' sent', screen.getvalue())
                            self.assertIsNone(logs.response_windows[monitor.SIDES[target_index]])
                        finally:
                            instance.close()
                            logs.close()

    def test_multibyte_short_write_stops_and_never_retries(self):
        for written in [0, 1, 6]:
            with self.subTest(written=written), tempfile.TemporaryDirectory() as folder, \
                    redirect_stdout(io.StringIO()):
                logs = monitor.MonitorLogs(Path(folder))
                ports = [FakeWritablePort(), FakeWritablePort(written)]
                instance = monitor.ConsoleMonitor(ports, logs, True)
                try:
                    instance.handle_input('r value 262143')
                    instance.handle_input('r value 30')
                    self.assertTrue(instance.stop.is_set())
                    self.assertEqual(ports[1].writes, [b'262143\r'])
                    self.assertEqual(ports[0].writes, [])
                    self.assertEqual(logs.commands[0]['status'], 'failed')
                    result = logs.commands[0]['results'][0]
                    self.assertEqual(result['bytes_requested'], 7)
                    self.assertEqual(result['bytes_written'], written)
                    self.assertIn(f'{written}/7', instance.errors.get_nowait())
                finally:
                    instance.close()
                    logs.close()

    def test_multibyte_write_exception_stops_and_audits_uncertain_delivery(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
            logs = monitor.MonitorLogs(Path(folder))
            ports = [FakeWritablePort(), FakeWritablePort(OSError('TX failed'))]
            instance = monitor.ConsoleMonitor(ports, logs, True)
            try:
                instance.handle_input('r value 30')
                self.assertTrue(instance.stop.is_set())
                result = logs.commands[0]['results'][0]
                self.assertEqual(result['bytes_requested'], 3)
                self.assertIsNone(result['bytes_written'])
                self.assertEqual(result['status'], 'failed')
                self.assertEqual(ports[1].writes, [b'30\r'])
            finally:
                instance.close()
                logs.close()

    def test_unterminated_menu_prompt_displays_once_and_preserves_disk(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)
            logs = monitor.MonitorLogs(output)
            prompt = b'  red margin [current 10, 0..255, Enter=keep]: '
            for byte in prompt:
                logs.feed('right', bytes([byte]))
            self.assertEqual(logs.display.qsize(), 1)
            self.assertIn('[R] ' + prompt.decode(), logs.display.get_nowait())
            self.assertEqual(logs.stats['right']['lines'], 0)
            logs.feed('right', b'30')
            logs.feed('right', b'\r\n')
            self.assertEqual(logs.display.qsize(), 1)
            self.assertTrue(logs.display.get_nowait().endswith('[R] 30'))
            logs.close()
            self.assertEqual((output / 'right_uart.raw').read_bytes(), prompt + b'30\r\n')
            self.assertEqual((output / 'right_uart.log').read_text(), prompt.decode() + '30\n')
            combined = (output / 'combined_uart.log').read_text()
            self.assertEqual(combined.count('Enter=keep'), 1)
            self.assertNotIn('[HOST:', combined)

    def test_prompts_display_with_stereo_filter_and_reset_for_next_field(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)
            logs = monitor.MonitorLogs(output, 'stereo')
            first = b'  red margin [current 10, 0..255, Enter=keep]: '
            second = b'  green margin [current 20, 0..255, Enter=keep]: '
            logs.feed('right', first)
            logs.feed('left', first)
            logs.feed('right', b'\r\n' + second)
            logs.feed('right', b'')
            self.assertEqual(logs.display.qsize(), 3)
            displays = [logs.display.get_nowait() for entry in range(3)]
            self.assertIn('[R] ' + first.decode(), displays[0])
            self.assertIn('[L] ' + first.decode(), displays[1])
            self.assertIn('[R] ' + second.decode(), displays[2])
            logs.close()
            self.assertEqual(logs.display.qsize(), 0)
            self.assertEqual((output / 'right_uart.raw').read_bytes(), first + b'\r\n' + second)
            self.assertEqual((output / 'right_uart.log').read_text().count('red margin'), 1)
            self.assertEqual((output / 'right_uart.log').read_text().count('green margin'), 1)

    def test_prompt_preview_queue_full_and_overlong_input_stay_bounded(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)
            logs = monitor.MonitorLogs(output, queue_size=1)
            prompt = b'  red margin [current 10, 0..255, Enter=keep]: '
            raw = b'fill queue\n' + b'x' * monitor.MAX_LINE_BYTES + prompt
            logs.feed('left', raw)
            self.assertEqual(logs.stats['left']['display_dropped'], 2)
            self.assertLess(len(logs.buffers['left']), monitor.MAX_LINE_BYTES)
            self.assertLess(logs.prompt_offsets['left'], monitor.MAX_LINE_BYTES)
            logs.feed('left', b'1')
            self.assertEqual(logs.stats['left']['display_dropped'], 2)
            logs.close()
            self.assertEqual((output / 'left_uart.raw').read_bytes(), raw + b'1')
            self.assertEqual((output / 'left_uart.log').read_text().count('Enter=keep'), 1)

    def test_multibyte_main_manifest_eof_and_no_automatic_resume(self):
        with tempfile.TemporaryDirectory() as folder:
            ports = [FakeWritablePort(), FakeWritablePort()]
            result = self.run_main(folder, ports, monitor.display_logs, ['--interactive'],
                                   io.StringIO('r m\nr value 30\nr enter\n'))
            self.assertEqual(result, 0)
            self.assertEqual(ports[1].writes, [b'm', b'30\r', b'\r'])
            self.assertTrue(all(port.closed for port in ports))
            session = json.loads((Path(folder) / 'session/session.json').read_text())
            self.assertEqual(session['commands'][1]['results'][0]['bytes_written'], 3)
            self.assertEqual(session['commands'][-1]['status'], 'eof')

    def test_both_submenu_main_eof_without_automatic_resume(self):
        with tempfile.TemporaryDirectory() as folder:
            ports = [FakeWritablePort(), FakeWritablePort()]
            result = self.run_main(folder, ports, monitor.display_logs,
                                   ['--interactive', '--filter', 'commands', '--format', 'plain'],
                                   io.StringIO('both m\nboth value 40\nboth enter\n'))
            self.assertEqual(result, 0)
            for port in ports:
                self.assertEqual(port.writes, [b'm', b'40\r', b'\r'])
                self.assertTrue(port.closed)
            session = json.loads((Path(folder) / 'session/session.json').read_text())
            self.assertEqual([event['status'] for event in session['commands']],
                             ['sent', 'sent', 'sent', 'eof'])
            self.assertEqual([result['bytes_written']
                              for result in session['commands'][1]['results']], [3, 3])

    def test_read_only_invalid_empty_and_blocked_inputs_never_transmit(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
            logs = monitor.MonitorLogs(Path(folder))
            ports = [FakePort(), FakePort()]
            instance = monitor.ConsoleMonitor(ports, logs)
            try:
                instance.handle_input('both ?')
                instance.input_queue.put('both ?')
                instance.process_input()
                self.assertFalse(instance.stop.is_set())
                self.assertEqual(logs.commands, [])
                instance.interactive = True
                for text in ['', '   ', '?', 'l 汉', 'r ??']:
                    instance.handle_input(text)
                for command in monitor.MOTION_COMMANDS:
                    instance.handle_input('both ' + command)
                self.assertEqual(sum(event['status'] == 'blocked' for event in logs.commands),
                                 len(monitor.MOTION_COMMANDS))
                self.assertEqual(sum(event['status'] == 'rejected' for event in logs.commands), 3)
            finally:
                instance.close()
                logs.close()

    def test_motion_opt_in_preserves_case_and_warns(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()) as screen:
            logs = monitor.MonitorLogs(Path(folder))
            ports = [FakeWritablePort(), FakeWritablePort()]
            instance = monitor.ConsoleMonitor(ports, logs, True, True)
            try:
                for command in ['R', 'P', 'E', 'X', 'A', 'S', 'T', 'V', 'u', 'h', 'f', 'j', 'i', 'k', 'r', 'p']:
                    instance.handle_input('r ' + command)
            finally:
                instance.close()
                logs.close()
            self.assertEqual(ports[0].writes, [])
            self.assertEqual(ports[1].writes, [b'R', b'P', b'E', b'\x1bX', b'A', b'\x1bS', b'T', b'V', b'u', b'h', b'f', b'j',
                                             b'i', b'k', b'r', b'p'])
            self.assertEqual(screen.getvalue().count('[HOST] WARNING:'), 11)

    def test_both_partial_failure_and_first_failure_no_retry(self):
        for failing_side in [0, 1]:
            with self.subTest(failing_side=failing_side), tempfile.TemporaryDirectory() as folder, \
                    redirect_stdout(io.StringIO()) as screen:
                logs = monitor.MonitorLogs(Path(folder), 'stereo')
                ports = [FakeWritablePort(), FakeWritablePort()]
                ports[failing_side].write_result = OSError('disconnected during write')
                instance = monitor.ConsoleMonitor(ports, logs, True)
                try:
                    instance.handle_input('both ?')
                    instance.handle_input('both ?')
                    self.assertTrue(instance.stop.is_set())
                    event = logs.commands[0]
                    self.assertEqual(len(logs.commands), 1)
                    self.assertEqual(event['results'][failing_side]['bytes_written'], None)
                    if failing_side == 1:
                        self.assertEqual(event['status'], 'partial')
                        self.assertEqual(event['results'][0]['bytes_written'], 1)
                        self.assertEqual(ports[1].writes, [b'?'])
                    else:
                        self.assertEqual(event['status'], 'failed')
                        self.assertEqual(event['results'][1]['status'], 'not_attempted')
                        self.assertEqual(ports[1].writes, [])
                    self.assertEqual(ports[0].writes, [b'?'])
                    self.assertIn('disconnected during write', instance.errors.get_nowait())
                    self.assertIn('"status": "failed"', screen.getvalue())
                finally:
                    instance.close()
                    logs.close()

    def test_short_write_stops_without_retry(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
            logs = monitor.MonitorLogs(Path(folder))
            ports = [FakeWritablePort(0), FakeWritablePort()]
            instance = monitor.ConsoleMonitor(ports, logs, True)
            try:
                instance.handle_input('both ?')
                self.assertTrue(instance.stop.is_set())
                self.assertEqual(logs.commands[0]['results'][0]['bytes_written'], 0)
                self.assertEqual(logs.commands[0]['results'][1]['status'], 'not_attempted')
                self.assertEqual(ports[0].writes, [b'?'])
                self.assertEqual(ports[1].writes, [])
            finally:
                instance.close()
                logs.close()

    def test_interactive_main_commands_and_local_exit_cleanup(self):
        real_display = monitor.display_logs
        with tempfile.TemporaryDirectory() as folder:
            ports = [FakeWritablePort(), FakeWritablePort()]
            result = self.run_main(folder, ports, real_display, ['--interactive', '--filter', 'stereo'],
                                   io.StringIO('l ?\nr C\nboth q\nq\nl x\n'))
            self.assertEqual(result, 0)
            self.assertEqual(ports[0].writes, [b'?', b'q'])
            self.assertEqual(ports[1].writes, [b'C', b'q'])
            self.assertTrue(all(port.closed for port in ports))
            session = json.loads((Path(folder) / 'session/session.json').read_text())
            self.assertFalse(session['receive_only'])
            self.assertTrue(session['interactive'])
            self.assertEqual(session['commands'][-1]['status'], 'exit')
            self.assertEqual(session['commands'][2]['target'], 'both')
            self.assertEqual((Path(folder) / 'session/left_uart.raw').read_bytes(), b'')

    def test_interactive_no_auto_tx_on_eof_or_open_failure(self):
        real_display = monitor.display_logs
        with tempfile.TemporaryDirectory() as folder:
            ports = [FakePort(), FakePort()]
            self.assertEqual(self.run_main(folder, ports, real_display, ['--interactive']), 0)
            self.assertTrue(all(port.closed for port in ports))
            session = json.loads((Path(folder) / 'session/session.json').read_text())
            self.assertEqual(session['commands'][0]['status'], 'eof')
        with tempfile.TemporaryDirectory() as folder:
            left = FakePort()
            self.assertEqual(self.run_main(folder, [left, OSError('busy')], real_display,
                                           ['--interactive'], io.StringIO('both x\n')), 1)
            self.assertTrue(left.closed)

    def test_partial_tx_failure_in_final_manifest(self):
        real_display = monitor.display_logs
        with tempfile.TemporaryDirectory() as folder:
            ports = [FakeWritablePort(), FakeWritablePort(OSError('TX failed'))]
            self.assertEqual(self.run_main(folder, ports, real_display, ['--interactive'],
                                           io.StringIO('both ?\nboth ?\n')), 1)
            self.assertTrue(all(port.closed for port in ports))
            session = json.loads((Path(folder) / 'session/session.json').read_text())
            self.assertEqual(session['status'], 'error')
            self.assertEqual(session['commands'][0]['status'], 'partial')
            self.assertIn('TX failed', session['errors'][0])
            self.assertEqual(ports[0].writes, [b'?'])
            self.assertEqual(ports[1].writes, [b'?'])

    def test_pending_stdin_does_not_block_rx_display_or_disconnect_cleanup(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()) as screen:
            logs = monitor.MonitorLogs(Path(folder), 'stereo')
            ports = [FakePort(), FakePort()]
            stdin = BlockingInput()
            instance = monitor.ConsoleMonitor(ports, logs, True)
            display = threading.Thread(target=monitor.display_logs, args=(instance,))
            try:
                with patch('sys.stdin', stdin):
                    instance.start()
                self.assertTrue(stdin.entered.wait(1))
                display.start()
                for port in ports:
                    port.incoming.put(b'[ST] received while waiting for stdin\n')
                wait_for(lambda: screen.getvalue().count('[ST] received') == 2)
                ports[1].incoming.put(OSError('unplugged'))
                wait_for(instance.stop.is_set)
                started = time.monotonic()
                instance.close()
                self.assertLess(time.monotonic() - started, 0.5)
                display.join(timeout=1)
                self.assertFalse(display.is_alive())
                self.assertTrue(instance.input_thread.daemon)
                self.assertTrue(instance.input_thread.is_alive())
                self.assertTrue(all(not thread.is_alive() for thread in instance.threads))
                self.assertEqual(logs.commands, [])
            finally:
                instance.close()
                stdin.release.set()
                if instance.input_thread is not None:
                    instance.input_thread.join(timeout=1)
                if display.ident is not None:
                    display.join(timeout=1)
                for port in ports:
                    port.close()
                logs.close()
            self.assertTrue(all(port.closed for port in ports))

    def test_interactive_ctrl_c_closes_ports_with_pending_stdin(self):
        with tempfile.TemporaryDirectory() as folder:
            ports = [FakePort(), FakePort()]
            stdin = BlockingInput()
            observed = []

            def interrupt(instance):
                observed.append(instance)
                self.assertTrue(stdin.entered.wait(1))
                raise KeyboardInterrupt

            try:
                self.assertEqual(self.run_main(folder, ports, interrupt, ['--interactive'], stdin), 130)
                self.assertTrue(all(port.closed for port in ports))
                session = json.loads((Path(folder) / 'session/session.json').read_text())
                self.assertEqual(session['status'], 'interrupted')
                self.assertEqual(session['commands'], [])
            finally:
                stdin.release.set()
                if observed:
                    observed[0].input_thread.join(timeout=1)

    def test_motion_option_requires_interactive_before_open(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()), \
                patch('sys.stderr', io.StringIO()):
            with self.assertRaises(SystemExit) as error:
                self.run_main(folder, [], extra_args=['--allow-motion-commands'])
            self.assertEqual(error.exception.code, 2)
            self.assertFalse((Path(folder) / 'session').exists())

    def test_raw_stdin_read_avoids_buffered_reader_and_rejects_non_ascii(self):
        commands = queue.Queue()
        stop = threading.Event()
        source = b'l ?\n' + 'r 한\n'.encode('utf-8')

        class DescriptorInput:
            def fileno(self):
                return 12345

            def readline(self):
                raise AssertionError('Do not hold the buffered stdin lock during shutdown')

        with patch.object(monitor.os, 'read', side_effect=[bytes([byte]) for byte in source] + [b'']):
            monitor.read_console_input(DescriptorInput(), commands, stop)
        self.assertEqual(monitor.parse_command(commands.get_nowait()), ('l', '?'))
        with self.assertRaises(ValueError):
            monitor.parse_command(commands.get_nowait())
        self.assertIsNone(commands.get_nowait())

    def test_stopped_queue_never_transmits_pending_input(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
            logs = monitor.MonitorLogs(Path(folder))
            instance = monitor.ConsoleMonitor([FakePort(), FakePort()], logs, True)
            try:
                instance.input_queue.put('both x')
                instance.stop.set()
                instance.process_input()
                instance.handle_input('both x')
                self.assertEqual(logs.commands, [])
            finally:
                instance.close()
                logs.close()

    def test_interrupted_write_is_audited_without_retry(self):
        real_display = monitor.display_logs
        with tempfile.TemporaryDirectory() as folder:
            ports = [FakeWritablePort(), FakeWritablePort(KeyboardInterrupt())]
            self.assertEqual(self.run_main(folder, ports, real_display, ['--interactive'],
                                           io.StringIO('both ?\n')), 1)
            self.assertTrue(all(port.closed for port in ports))
            session = json.loads((Path(folder) / 'session/session.json').read_text())
            self.assertEqual(session['commands'][0]['status'], 'partial')
            self.assertIsNone(session['commands'][0]['results'][1]['bytes_written'])
            self.assertIn('KeyboardInterrupt', session['errors'][0])

    def test_stdin_failure_stops_and_records_error_without_tx(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
            logs = monitor.MonitorLogs(Path(folder))
            instance = monitor.ConsoleMonitor([FakePort(), FakePort()], logs, True)
            try:
                instance.input_queue.put(OSError('stdin closed'))
                instance.process_input()
                self.assertTrue(instance.stop.is_set())
                self.assertEqual(logs.commands[0]['status'], 'input_error')
                self.assertIn('stdin closed', instance.errors.get_nowait())
            finally:
                instance.close()
                logs.close()

    def test_commands_filter_silences_unsolicited_lines_but_keeps_disk(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)
            logs = monitor.MonitorLogs(output, 'commands')
            raw = b'boot\nTK,1\n[ST] tx=2\nred margin [current 1, 0..255, Enter=keep]: '
            logs.feed('right', raw)
            self.assertEqual(logs.display.qsize(), 0)
            logs.close()
            self.assertEqual(logs.display.qsize(), 0)
            self.assertEqual((output / 'right_uart.raw').read_bytes(), raw)
            self.assertIn('boot\n', (output / 'right_uart.log').read_text())
            self.assertIn('[ST] tx=2', (output / 'combined_uart.log').read_text())

    def test_response_window_armed_before_write_and_targets_multiline_help(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
            logs = monitor.MonitorLogs(Path(folder), 'commands', display_format='plain')

            class ImmediateReply(FakeWritablePort):
                def write(self, data):
                    self.writes.append(data)
                    logs.feed('left', b'unsolicited left\n')
                    logs.feed('right', b'Help header\r\n  a: auto inference\r\n'
                              b'TK,1\nCN,2\n[ST] tx=3\nHelp footer\n')
                    return len(data)

            ports = [FakePort(), ImmediateReply()]
            instance = monitor.ConsoleMonitor(ports, logs, True)
            try:
                instance.handle_input('r ?')
                displays = [logs.display.get_nowait() for entry in range(logs.display.qsize())]
                self.assertEqual(displays, ['[R] Help header', '[R]   a: auto inference',
                                            '[R] Help footer'])
                self.assertEqual(ports[1].writes, [b'?'])
            finally:
                instance.close()
                logs.close()

    def test_commands_filter_st_status_only_for_explicit_async_keys(self):
        with tempfile.TemporaryDirectory() as folder:
            logs = monitor.MonitorLogs(Path(folder), 'commands', display_format='plain')
            reply = b'[ST] async_test=0 pwm=1 result=status mode=LIVE\n'
            for command in ['T', 'A', 'S', 'X']:
                logs.arm_response('right', command)
                logs.feed('right', b'[ST] tx=3 async_test=0\n' + reply)
                self.assertEqual(logs.display.qsize(), 1)
                self.assertEqual(logs.display.get_nowait(), '[R] ' + reply.decode().strip())
            logs.arm_response('right', 'p')
            logs.feed('right', reply)
            self.assertEqual(logs.display.qsize(), 0)
            logs.close()

    def test_coordinate_telemetry_hidden_in_commands_and_console_but_saved(self):
        for display_filter in ['commands', 'console']:
            with self.subTest(display_filter=display_filter), tempfile.TemporaryDirectory() as folder:
                output = Path(folder)
                logs = monitor.MonitorLogs(output, display_filter, display_format='plain')
                if display_filter == 'commands':
                    logs.arm_response('right', 'r')
                telemetry = b''.join(tag.encode() + suffix for tag in ['RAW', 'PAIR', 'PIX', 'PG', 'RQ']
                                     for suffix in [b',1,2,3\n', b',sid,pair_id\n'])
                raw = telemetry + b'CNN result: wrist=100,120\n' + b''.join(
                    b'#' + tag.encode() + b',sid,pair_id\n' for tag in ['RAW', 'PAIR', 'PIX', 'PG'])
                logs.feed('right', raw)
                self.assertEqual(logs.display.qsize(), 1)
                self.assertEqual(logs.display.get_nowait(), '[R] CNN result: wrist=100,120')
                logs.close()
                self.assertEqual((output / 'right_uart.raw').read_bytes(), raw)
                self.assertEqual((output / 'right_uart.log').read_text(), raw.decode())
                combined = (output / 'combined_uart.log').read_text()
                for tag in ['RAW', 'PAIR', 'PIX', 'PG']:
                    self.assertIn('[R] ' + tag + ',1,2,3', combined)
                    self.assertIn('[R] #' + tag + ',sid,pair_id', combined)

    def test_response_window_expiry_and_fragment_start_are_not_retroactive(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(
                monitor.time, 'monotonic', return_value=100.0) as clock:
            logs = monitor.MonitorLogs(Path(folder), 'commands', response_window=2.0,
                                       display_format='plain')
            logs.feed('right', b'old partial')
            logs.arm_response('right', '?')
            logs.feed('right', b' line\nnew ')
            logs.feed('right', b'reply\n')
            self.assertEqual(logs.display.get_nowait(), '[R] new reply')
            self.assertEqual(logs.display.qsize(), 0)
            clock.return_value = 103.0
            logs.feed('right', b'delayed reply\n')
            self.assertEqual(logs.display.qsize(), 0)
            logs.arm_response('right', 'p')
            logs.feed('right', b'fragmented ')
            clock.return_value = 106.0
            logs.feed('right', b'too late\n')
            self.assertEqual(logs.display.qsize(), 0)
            logs.close()

    def test_commands_menu_prompts_fragment_and_rearm_after_thinking(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()), \
                patch.object(monitor.time, 'monotonic', return_value=100.0) as clock:
            output = Path(folder)
            logs = monitor.MonitorLogs(output, 'commands', display_format='plain')
            ports = [FakeWritablePort(), FakeWritablePort()]
            instance = monitor.ConsoleMonitor(ports, logs, True)
            first = b'  red margin [current 10, 0..255, Enter=keep]: '
            second = b'  green margin [current 20, 0..255, Enter=keep]: '
            raw = b'CNN RGBY detection settings\r\n' + first + b'30\r\n' + second
            try:
                instance.handle_input('r m')
                logs.feed('right', b'CNN RGBY detection settings\r\n')
                for byte in first:
                    logs.feed('right', bytes([byte]))
                self.assertEqual(logs.display.qsize(), 2)
                clock.return_value = 120.0
                instance.handle_input('r value 30')
                logs.feed('right', b'30\r\n' + second)
                displays = [logs.display.get_nowait() for entry in range(logs.display.qsize())]
                self.assertEqual(displays, ['[R] CNN RGBY detection settings',
                                            '[R] ' + first.decode(), '[R] ' + second.decode()])
                self.assertEqual(ports[1].writes, [b'm', b'30\r'])
                clock.return_value = 130.0
            finally:
                instance.close()
                logs.close()
            self.assertEqual(logs.display.qsize(), 0)
            self.assertEqual((output / 'right_uart.raw').read_bytes(), raw)
            self.assertEqual((output / 'right_uart.log').read_text().count('red margin'), 1)
            self.assertEqual((output / 'right_uart.log').read_text().count('green margin'), 1)

    def test_rejected_blocked_exit_and_failed_writes_do_not_leave_windows(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
            logs = monitor.MonitorLogs(Path(folder), 'commands')
            ports = [FakeWritablePort(), FakeWritablePort(0)]
            instance = monitor.ConsoleMonitor(ports, logs, True)
            try:
                instance.handle_input('r A')
                instance.handle_input('r value E')
                self.assertIsNone(logs.response_windows['right'])
                instance.handle_input('r ?')
                self.assertIsNone(logs.response_windows['right'])
                self.assertEqual(ports[1].writes, [b'?'])
                self.assertTrue(instance.stop.is_set())
                logs.feed('right', b'no reply after failure\n')
                self.assertEqual(logs.display.qsize(), 0)
            finally:
                instance.close()
                logs.close()

    def test_commands_successive_menu_prompts_without_newlines_or_echo(self):
        for echo in [b'', b'30']:
            with self.subTest(echo=echo), tempfile.TemporaryDirectory() as folder, \
                    redirect_stdout(io.StringIO()):
                output = Path(folder)
                logs = monitor.MonitorLogs(output, 'commands', display_format='plain')
                instance = monitor.ConsoleMonitor([FakeWritablePort(), FakeWritablePort()], logs, True)
                first = b'  red margin [current 10, 0..255, Enter=keep]: '
                second = b'  green margin [current 20, 0..255, Enter=keep]: '
                third = b'  blue margin [current 40, 0..255, Enter=keep]: '
                try:
                    instance.handle_input('r m')
                    logs.feed('right', first)
                    instance.handle_input('r value 30')
                    for byte in echo + second:
                        logs.feed('right', bytes([byte]))
                    instance.handle_input('r enter')
                    logs.feed('right', third)
                    displays = [logs.display.get_nowait() for entry in range(logs.display.qsize())]
                    self.assertEqual(displays, ['[R] ' + first.decode(),
                                                '[R] ' + (echo + second).decode(),
                                                '[R] ' + third.decode()])
                    logs.feed('right', b'')
                    self.assertEqual(logs.display.qsize(), 0)
                finally:
                    instance.close()
                    logs.close()
                self.assertEqual((output / 'right_uart.raw').read_bytes(), first + echo + second + third)
                combined = (output / 'combined_uart.log').read_text()
                self.assertEqual(combined.count('red margin'), 1)
                self.assertEqual(combined.count('green margin'), 1)
                self.assertEqual(combined.count('blue margin'), 1)

    def test_both_commands_open_independent_target_windows(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
            logs = monitor.MonitorLogs(Path(folder), 'commands', display_format='plain')
            ports = [FakeWritablePort(), FakeWritablePort()]
            instance = monitor.ConsoleMonitor(ports, logs, True)
            try:
                instance.handle_input('both ?')
                logs.feed('left', b'left help\n')
                logs.feed('right', b'right help\n')
                self.assertEqual(logs.display.get_nowait(), '[L] left help')
                self.assertEqual(logs.display.get_nowait(), '[R] right help')
                self.assertNotEqual(logs.response_windows['left']['id'],
                                    logs.response_windows['right']['id'])
            finally:
                instance.close()
                logs.close()

    def test_both_submenu_prompts_rearm_independently_after_window_expiry(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()), \
                patch.object(monitor.time, 'monotonic', return_value=100.0) as clock:
            logs = monitor.MonitorLogs(Path(folder), 'commands', display_format='plain')
            instance = monitor.ConsoleMonitor([FakeWritablePort(), FakeWritablePort()], logs, True)
            first = b'  red margin [current 40, 0..255, Enter=keep]: '
            second = b'  green margin [current 30, 0..255, Enter=keep]: '
            third = b'  blue margin [current 50, 0..255, Enter=keep]: '
            try:
                instance.handle_input('both m')
                for side in monitor.SIDES:
                    logs.feed(side, first)
                clock.return_value = 110.0
                instance.handle_input('both value 40')
                for side in monitor.SIDES:
                    for byte in second:
                        logs.feed(side, bytes([byte]))
                clock.return_value = 120.0
                instance.handle_input('both enter')
                for side in monitor.SIDES:
                    logs.feed(side, third)
                displays = [logs.display.get_nowait() for entry in range(logs.display.qsize())]
                self.assertEqual(displays, [f'[{label}] {prompt.decode()}'
                                            for prompt in [first, second, third]
                                            for label in ['L', 'R']])
                self.assertNotEqual(logs.response_windows['left']['id'],
                                    logs.response_windows['right']['id'])
            finally:
                instance.close()
                logs.close()

    def test_plain_format_compact_host_results_and_full_json_audit(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()) as screen:
            output = Path(folder)
            logs = monitor.MonitorLogs(output, display_format='plain')
            instance = monitor.ConsoleMonitor([FakeWritablePort(), FakeWritablePort()], logs, True)
            try:
                instance.handle_input('r value 30')
                instance.handle_input('r q')
                instance.handle_input('r E')
                logs.feed('right', b'UART output resumed\n')
                self.assertEqual(logs.display.get_nowait(), '[R] UART output resumed')
            finally:
                instance.close()
                logs.close()
            text = screen.getvalue()
            self.assertIn('[HOST] r value 30: sent (R 3/3 sent)', text)
            self.assertIn('[HOST] r E: blocked', text)
            self.assertIn('muting may have no reply', text)
            self.assertNotIn('timestamp_utc', text)
            self.assertNotIn('"results"', text)
            audit = [json.loads(line) for line in
                     (output / 'host_commands.jsonl').read_text().splitlines()]
            self.assertEqual(audit, logs.commands)
            self.assertIn('timestamp_utc', audit[0])
            self.assertEqual(audit[0]['command'], '30\r')
            self.assertRegex((output / 'combined_uart.log').read_text(), r'\d{4}-\d{2}-\d{2}T')

    def test_command_filter_cli_requires_interactive_and_finite_window(self):
        for args in [['--filter', 'commands'], ['--response-window', '0'],
                     ['--response-window', '-1'], ['--response-window', 'nan'],
                     ['--response-window', 'inf']]:
            with self.subTest(args=args), tempfile.TemporaryDirectory() as folder, \
                    redirect_stdout(io.StringIO()), patch('sys.stderr', io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    self.run_main(folder, [], extra_args=args)
                self.assertEqual(error.exception.code, 2)
                self.assertFalse((Path(folder) / 'session').exists())

    def test_command_filter_plain_main_manifest_eof_without_automatic_tx(self):
        with tempfile.TemporaryDirectory() as folder:
            ports = [FakeWritablePort(), FakeWritablePort()]
            result = self.run_main(folder, ports, monitor.display_logs,
                                   ['--interactive', '--filter', 'commands', '--format', 'plain',
                                    '--response-window', '2.5'], io.StringIO('r ?\n'))
            self.assertEqual(result, 0)
            self.assertEqual(ports[1].writes, [b'?'])
            self.assertEqual(ports[0].writes, [])
            session = json.loads((Path(folder) / 'session/session.json').read_text())
            self.assertEqual(session['screen_filter'], 'commands')
            self.assertEqual(session['screen_format'], 'plain')
            self.assertEqual(session['response_window_seconds'], 2.5)
            self.assertEqual(session['commands'][-1]['status'], 'eof')

    def test_record_state_restart_restores_name_without_startup_or_exit_tx(self):
        with tempfile.TemporaryDirectory() as folder:
            ports = [FakeWritablePort(), FakeWritablePort()]
            raw = (b'[REC_STATE] schema=1 side=r ui=NAME mode=HOLDING pwm=0 '
                   b'ram_samples=234 entries=2 selected=1 playing=0 delete=0 unsaved=1\n')

            def receive_status(instance):
                self.assertEqual([port.writes for port in ports], [[], []])
                instance.logs.feed('right', raw)
                record = instance.logs.records['right']
                self.assertTrue(record.name_prompt)
                self.assertTrue(record.unsaved)
                self.assertEqual(record.ram_samples, 234)
                self.assertFalse(record.menu)
                self.assertIn('RAM 234개 / SD 미저장', instance.logs.display.get_nowait())
                instance.stop.set()

            result = self.run_main(folder, ports, receive_status,
                                   ['--interactive', '--filter', 'commands', '--no-video'])
            self.assertEqual(result, 0)
            self.assertEqual([port.writes for port in ports], [[], []])
            self.assertTrue(all(port.closed for port in ports))
            output = Path(folder) / 'session'
            self.assertEqual((output / 'right_uart.raw').read_bytes(), raw)
            self.assertEqual(json.loads((output / 'session.json').read_text())['commands'], [])

    def test_record_ui_busy_restores_name_without_automatic_status_query(self):
        with tempfile.TemporaryDirectory() as folder:
            ports = [FakeWritablePort(), FakeWritablePort()]

            def rejected_menu(instance):
                record = instance.logs.records['left']
                record.menu = True
                record.entries = {1: 'old_record'}
                instance.handle_input('l P')
                instance.logs.feed('left', b'[REC] rejected UI_BUSY; finish naming or l record cancel\n'
                                   b'[REC_STATE] schema=1 side=l ui=NAME mode=HOLDING pwm=1 '
                                   b'ram_samples=100 entries=1 selected=0 playing=0 delete=0 unsaved=1\n')
                self.assertTrue(record.name_prompt)
                self.assertFalse(record.menu)
                self.assertEqual([port.writes for port in ports], [[b'P'], []])
                instance.stop.set()

            result = self.run_main(folder, ports, rejected_menu,
                                   ['--interactive', '--allow-motion-commands',
                                    '--filter', 'commands', '--no-video'])
            self.assertEqual(result, 0)
            self.assertEqual([port.writes for port in ports], [[b'P'], []])
            session = json.loads((Path(folder) / 'session/session.json').read_text())
            self.assertEqual([event['command'] for event in session['commands']], ['P'])


if __name__ == '__main__':
    unittest.main()
