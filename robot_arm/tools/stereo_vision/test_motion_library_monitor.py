import io
from pathlib import Path
from contextlib import redirect_stdout
import tempfile
import unittest
from unittest.mock import patch
import queue

import monitor_stereo_uart as monitor
from test_monitor_stereo_uart import FakeWritablePort


class MotionLibraryMonitorTests(unittest.TestCase):
    def test_windows_keyboard_menu_keys_and_regular_line_editing(self):
        import msvcrt

        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
            logs = monitor.MonitorLogs(Path(folder))
            instance = monitor.ConsoleMonitor([FakeWritablePort(), FakeWritablePort()], logs, True, True)
            logs.records['right'].menu = True
            logs.records['right'].entries = {1: 'basic_1'}
            commands = queue.Queue()
            keys = iter('11\x08\rr Vx\x08\r\x03')
            with patch.object(msvcrt, 'kbhit', return_value=True), patch.object(msvcrt, 'getwch', side_effect=lambda: next(keys)):
                monitor.read_windows_console(commands, instance.stop, instance)
            actual = [commands.get_nowait() for _ in range(commands.qsize())]
            self.assertEqual(actual, [('key', '1', 'right'), ('key', '1', 'right'), ('key', '\x08', 'right'),
                                      ('key', '\r', 'right'), 'r V\n', 'q\n'])
            logs.close()
    def test_readable_record_menu_keeps_original_disk_log(self):
        with tempfile.TemporaryDirectory() as folder:
            logs = monitor.MonitorLogs(Path(folder), 'commands', display_format='plain')
            logs.arm_response('right', 'P')
            raw = (b'[REC] SD records; select: r record select <number>\r\n'
                   b'[REC] 1 basic_1 samples=935 duration_ms=18700\r\n'
                   b'[REC] 3 grab_2 samples=500 duration_ms=10000 selected\r\n'
                   b'[REC] count=2; delete: r record delete <number>; cancel: r record cancel\r\n'
                   b'#EV,t_ms,code,arg\r\n#CE,fid,t_ms,error\r\n')
            for byte in raw:
                logs.feed('right', bytes([byte]))
            shown = [logs.display.get_nowait() for _ in range(logs.display.qsize())]
            self.assertEqual(shown, ['[R] === 재생할 레코드 선택 ===', '[R] 1. basic_1',
                                    '[R] 3. grab_2', '[R] 번호 선택 → 같은 번호: 재생 / Backspace → Enter: 삭제 / Esc: 취소'])
            logs.close()
            self.assertIn('samples=935', (Path(folder) / 'combined_uart.log').read_text(encoding='utf-8'))

    def test_immediate_menu_play_and_confirmed_delete(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
            logs = monitor.MonitorLogs(Path(folder))
            ports = [FakeWritablePort(), FakeWritablePort()]
            instance = monitor.ConsoleMonitor(ports, logs, True, True)
            self.assertFalse(instance.handle_menu_key('1'))
            menu = (b'[REC] SD records; select: r record select <number>\n'
                    b'[REC] 1 basic_1 samples=935 duration_ms=18700\n'
                    b'[REC] count=1; cancel: r record cancel\n')
            logs.feed('right', menu)
            instance.handle_menu_key('1')
            self.assertEqual(ports[1].writes, [])
            instance.handle_menu_key('1')
            self.assertEqual(ports[1].writes, [b'!REC,SELECT,1\r'])
            self.assertTrue(logs.records['right'].menu)
            self.assertIsNotNone(logs.records['right'].select_pending)
            instance.handle_menu_key('1')
            self.assertEqual(len(ports[1].writes), 1)
            logs.feed('right', b'[REC] PLAY id=1 name=basic_1 samples=935 repeat=1\n')
            self.assertFalse(logs.records['right'].menu)
            self.assertIsNone(logs.records['right'].select_pending)
            logs.feed('right', menu)
            instance.handle_menu_key('1')
            instance.handle_menu_key('\x08')
            self.assertEqual(ports[1].writes[-1], b'!REC,DELETE,1\r')
            instance.handle_menu_key('\r')
            self.assertEqual(len(ports[1].writes), 2)
            logs.feed('right', b'[REC] confirm deleting 1 basic_1: r record confirm 1; cancel: r record cancel\n')
            instance.handle_menu_key('\r')
            self.assertEqual(ports[1].writes[-1], b'!REC,CONFIRM,1\r')
            logs.feed('right', b'[REC] DELETED id=1\n')
            self.assertFalse(logs.records['right'].menu)
            self.assertEqual(logs.records['right'].last_deleted, (1, 'basic_1'))
            self.assertEqual(ports[0].writes, [])
            logs.close()

    def test_menu_cancel_and_motion_gate(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
            logs = monitor.MonitorLogs(Path(folder))
            ports = [FakeWritablePort(), FakeWritablePort()]
            instance = monitor.ConsoleMonitor(ports, logs, True, False)
            logs.records['right'].menu = True
            logs.records['right'].entries = {1: 'basic_1'}
            instance.handle_menu_key('1')
            instance.handle_menu_key('1')
            self.assertEqual(ports[1].writes, [])
            self.assertTrue(logs.records['right'].menu)
            self.assertIsNone(logs.records['right'].select_pending)
            logs.records['right'].menu = True
            instance.handle_menu_key('\x1b')
            self.assertEqual(ports[1].writes, [b'!REC,CANCEL\r'])
            self.assertFalse(logs.records['right'].menu)
            logs.close()

    def test_rejected_selection_keeps_menu_and_can_retry(self):
        for rejection in (b'[REC] select rejected IO reason=NONE; pose held\n',
                          b'[REC] play rejected reason=DELTA; pose held\n',
                          b'[REC] rejected FRAME_OR_MENU_OR_BUSY\n'):
            with self.subTest(rejection=rejection), tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
                logs = monitor.MonitorLogs(Path(folder))
                ports = [FakeWritablePort(), FakeWritablePort()]
                instance = monitor.ConsoleMonitor(ports, logs, True, True)
                logs.records['right'].menu = True
                logs.records['right'].entries = {1: 'basic_1'}
                instance.handle_menu_key('1')
                instance.handle_menu_key('1')
                logs.feed('right', rejection)
                self.assertTrue(logs.records['right'].menu)
                self.assertIsNone(logs.records['right'].select_pending)
                instance.handle_menu_key('1')
                instance.handle_menu_key('1')
                self.assertEqual(ports[1].writes, [b'!REC,SELECT,1\r'] * 2)
                logs.close()

    def test_selection_timeout_keeps_menu_without_automatic_retry(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
            logs = monitor.MonitorLogs(Path(folder))
            ports = [FakeWritablePort(), FakeWritablePort()]
            instance = monitor.ConsoleMonitor(ports, logs, True, True)
            logs.records['right'].menu = True
            logs.records['right'].entries = {1: 'basic_1'}
            with patch.object(monitor.time, 'monotonic', return_value=10.0):
                instance.handle_input('r record select 1')
                instance.handle_input('r record select 1')
                self.assertEqual(ports[1].writes, [b'!REC,SELECT,1\r'])
            with patch.object(monitor.time, 'monotonic', return_value=15.0):
                instance.process_input()
                instance.process_input()
            self.assertTrue(logs.records['right'].menu)
            self.assertIsNone(logs.records['right'].select_pending)
            self.assertEqual(ports[1].writes, [b'!REC,SELECT,1\r'])
            self.assertEqual(sum(event['status'] == 'response_timeout' for event in logs.commands), 1)
            logs.feed('right', b'[REC] PLAY id=1 name=basic_1 samples=935 repeat=1\n')
            self.assertFalse(logs.records['right'].menu)
            self.assertEqual(logs.records['right'].playing, (1, 'basic_1'))
            logs.close()

    def test_selection_tx_failure_and_boot_reset_clear_pending(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
            logs = monitor.MonitorLogs(Path(folder))
            ports = [FakeWritablePort(), FakeWritablePort(0)]
            instance = monitor.ConsoleMonitor(ports, logs, True, True)
            logs.records['right'].menu = True
            logs.records['right'].entries = {1: 'basic_1'}
            instance.handle_input('r record select 1')
            self.assertTrue(logs.records['right'].menu)
            self.assertIsNone(logs.records['right'].select_pending)
            logs.records['right'].select_pending = {'id': 1, 'until': 1e20}
            logs.feed('right', b' Zybo Z7-20 CNN camera platform, 720p60\n')
            self.assertIsNone(logs.records['right'].select_pending)
            self.assertFalse(logs.records['right'].menu)
            logs.close()

    def test_stop_escape_preserves_case_and_audit_and_reports_capability(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()) as screen:
            logs = monitor.MonitorLogs(Path(folder), 'commands')
            ports = [FakeWritablePort(), FakeWritablePort()]
            instance = monitor.ConsoleMonitor(ports, logs, True, True)
            logs.feed('right', b'[UART] stop_escape=1 protocol=ESC_X_S frame_timeout_ms=250 role=1\n')
            self.assertFalse(logs.uart_stop_supported['right'])
            logs.feed('right', b'[UART] stop_escape=1 protocol=ESC_X_S frame_timeout_ms=250 role=2\n')
            self.assertTrue(logs.uart_stop_supported['right'])
            instance.handle_input('r X')
            instance.handle_input('r S')
            instance.handle_input('r x')
            self.assertEqual(ports[1].writes, [b'\x1bX', b'\x1bS', b'x'])
            self.assertEqual([event['command'] for event in logs.commands], ['X', 'S', 'x'])
            self.assertEqual(logs.commands[0]['results'][0]['bytes_written'], 2)
            self.assertNotIn('수신 취소 지원 미확인', screen.getvalue())
            instance.handle_input('both S')
            self.assertEqual(ports[0].writes, [b'\x1bS'])
            self.assertEqual(ports[1].writes[-1], b'\x1bS')
            self.assertIn('수신 취소 지원 미확인: L', screen.getvalue())
            logs.feed('right', b' Zybo Z7-20 CNN camera platform, 720p60\n')
            self.assertFalse(logs.uart_stop_supported['right'])
            logs.close()

    def test_number_enter_selection(self):
        for text in ('1\n', '01\r\n', ' 1 '):
            self.assertEqual(monitor.parse_command(text), ('r', '!REC,SELECT,1\r'))
        for text in ('0', '33', '999', '1 E'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                monitor.parse_command(text)
        self.assertIsNone(monitor.parse_command('\n'))

    def test_number_selection_requires_motion_opt_in(self):
        for enabled in (False, True):
            with self.subTest(enabled=enabled), tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
                logs = monitor.MonitorLogs(Path(folder))
                ports = [FakeWritablePort(), FakeWritablePort()]
                instance = monitor.ConsoleMonitor(ports, logs, True, enabled)
                instance.handle_input('1\n')
                self.assertEqual(ports[0].writes, [])
                self.assertEqual(ports[1].writes, [b'!REC,SELECT,1\r'] if enabled else [])
                logs.close()

    def test_library_payloads(self):
        for text, payload in [('r record list', '!REC,LIST\r'),
                              ('r record name basic_1', '!REC,NAME,basic_1\r'),
                              ('r record select 02', '!REC,SELECT,2\r'),
                              ('r record delete 2', '!REC,DELETE,2\r'),
                              ('r record confirm 2', '!REC,CONFIRM,2\r'),
                              ('r record cancel', '!REC,CANCEL\r')]:
            with self.subTest(text=text):
                self.assertEqual(monitor.parse_command(text), ('r', payload))

    def test_settings_commands(self):
        for target in ('l', 'r', 'both'):
            for action in ('show', 'save', 'load'):
                self.assertEqual(monitor.parse_command(f'{target} settings {action}'),
                                 (target, f'@CFG,{action.upper()}\r'))
        with self.assertRaises(ValueError):
            monitor.parse_command('r settings reset')

    def test_old_firmware_settings_are_never_transmitted(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
            logs = monitor.MonitorLogs(Path(folder))
            ports = [FakeWritablePort(), FakeWritablePort()]
            instance = monitor.ConsoleMonitor(ports, logs, True, True)
            instance.handle_input('r settings save')
            self.assertEqual(ports[1].writes, [])
            logs.feed('right', b'[CFG] supported=1 role=1 protocol=@CFG schema=1;\n')
            instance.handle_input('r settings save')
            self.assertEqual(ports[1].writes, [])
            logs.feed('right', b'[CFG] supported=1 role=2 protocol=@CFG schema=1;\n')
            instance.handle_input('r settings show')
            self.assertEqual(ports[1].writes, [b'@CFG,SHOW\r'])
            instance.handle_input('both settings save')
            self.assertEqual(ports[0].writes, [])
            self.assertEqual(ports[1].writes, [b'@CFG,SHOW\r'])
            logs.feed('right', b' Zybo Z7-20 CNN camera platform, 720p60\n')
            instance.handle_input('r settings load')
            self.assertEqual(ports[1].writes, [b'@CFG,SHOW\r'])
            logs.close()

    def test_direct_name_input_and_status(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
            logs = monitor.MonitorLogs(Path(folder), 'commands', display_format='plain')
            ports = [FakeWritablePort(), FakeWritablePort()]
            instance = monitor.ConsoleMonitor(ports, logs, True, True)
            logs.feed('right', b'[REC] enter name: r record name <name>\n')
            self.assertTrue(logs.records['right'].name_prompt)
            instance.handle_input('grab_2\n')
            self.assertEqual(ports[1].writes, [b'!REC,NAME,grab_2\r'])
            logs.feed('right', b'[REC] SAVED id=2 name=grab_2 samples=100\n')
            self.assertFalse(logs.records['right'].name_prompt)
            logs.feed('right', b'[REC] PLAY id=2 name=grab_2 samples=100 repeat=1\n')
            self.assertEqual(logs.records['right'].playing, (2, 'grab_2'))
            logs.feed('right', b'[REC] playback stop=1 mode=LIVE\n')
            self.assertIsNone(logs.records['right'].playing)
            shown = [logs.display.get_nowait() for _ in range(logs.display.qsize())]
            self.assertTrue(any('SD 저장 완료: 2. grab_2' in line for line in shown))
            self.assertTrue(any('재생 시작: 2. grab_2' in line for line in shown))
            self.assertTrue(any('재생 중지 완료' in line for line in shown))
            logs.close()

    def test_name_prompt_rejects_paths_and_cfg_survives_response_timeout(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
            logs = monitor.MonitorLogs(Path(folder), 'commands')
            ports = [FakeWritablePort(), FakeWritablePort()]
            instance = monitor.ConsoleMonitor(ports, logs, True, True)
            logs.records['right'].name_prompt = True
            instance.handle_input('../bad')
            self.assertEqual(ports[1].writes, [])
            instance.handle_input('r V')
            self.assertEqual(ports[1].writes, [b'V'])
            logs.feed('right', b'[CFG] SAVED path=0:/UARTCFG.BIN\n')
            self.assertIn('[CFG] SAVED', logs.display.get_nowait())
            logs.close()

    def test_invalid_library_commands(self):
        for text in ['both record list', 'both record name bad', 'r record name ../bad',
                     'r record name bad/name', 'r record name a b', 'r record name ' + 'a' * 25,
                     'r record select 0', 'r record select 33', 'r record select 999',
                     'r record confirm -1', 'r record delete 1;E', 'r record unknown']:
            with self.subTest(text=text), self.assertRaises(ValueError):
                monitor.parse_command(text)

    def test_select_requires_motion_opt_in(self):
        for enabled in (False, True):
            with self.subTest(enabled=enabled), tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
                logs = monitor.MonitorLogs(Path(folder))
                ports = [FakeWritablePort(), FakeWritablePort()]
                instance = monitor.ConsoleMonitor(ports, logs, True, enabled)
                instance.handle_input('r record select 1')
                self.assertEqual(ports[0].writes, [])
                self.assertEqual(ports[1].writes, [b'!REC,SELECT,1\r'] if enabled else [])
                logs.close()

    def test_delayed_name_prompt_remains_visible(self):
        with tempfile.TemporaryDirectory() as folder:
            logs = monitor.MonitorLogs(Path(folder), 'commands')
            logs.feed('right', b'[REC] enter name: r record name <name>\r\n')
            self.assertIn('저장할 이름', logs.display.get_nowait())
            logs.feed('right', b'TK,1,2,3\r\n')
            self.assertTrue(logs.display.empty())
            logs.close()


class SplitBoardMonitorTests(unittest.TestCase):
    def setUp(self):
        self.output = Path(self.enterContext(tempfile.TemporaryDirectory()))
        self.screen = io.StringIO()
        self.enterContext(redirect_stdout(self.screen))
        self.logs = monitor.MonitorLogs(self.output, 'commands', display_format='plain',
                                        response_window=1.0)
        self.addCleanup(self.logs.close)
        self.ports = [FakeWritablePort(), FakeWritablePort()]
        self.instance = monitor.ConsoleMonitor(self.ports, self.logs, True, True)

    def menu(self, side):
        target = side[0]
        raw = (f'[REC] SD records; select: {target} record select <number>\r\n'
               f'[REC] 1 {side}_1 samples=100 duration_ms=2000\r\n'
               f'[REC] count=1; delete: {target} record delete <number>; '
               f'cancel: {target} record cancel\r\n').encode('ascii')
        self.logs.feed(side, raw)
        return raw

    def test_left_library_payloads_and_single_target_validation(self):
        for action, payload in (('list', '!REC,LIST\r'), ('name left_1', '!REC,NAME,left_1\r'),
                                ('select 02', '!REC,SELECT,2\r'), ('delete 2', '!REC,DELETE,2\r'),
                                ('confirm 2', '!REC,CONFIRM,2\r'), ('cancel', '!REC,CANCEL\r')):
            with self.subTest(action=action):
                self.assertEqual(monitor.parse_command(f'l record {action}'), ('l', payload))
        for text in ('both record list', 'both record select 1', 'l record name ../bad',
                     'l record confirm 0', 'l record delete 33', 'l record name a b'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                monitor.parse_command(text)

    def test_left_and_right_control_bytes_remain_local(self):
        commands = ('E', 'A', 'S', 'T', 'V', 'R', 'P', 'X')
        expected = [b'E', b'A', b'\x1bS', b'T', b'V', b'R', b'P', b'\x1bX']
        for target in ('l', 'r'):
            for command in commands:
                self.instance.handle_input(f'{target} {command}')
        self.assertEqual(self.ports[0].writes, expected)
        self.assertEqual(self.ports[1].writes, expected)
        self.assertEqual([event['target'] for event in self.logs.commands], ['l'] * 8 + ['r'] * 8)

    def test_delayed_fragmented_left_menu_and_arm_source_logs(self):
        with patch.object(monitor.time, 'monotonic', return_value=10.0):
            self.instance.handle_input('l P')
        with patch.object(monitor.time, 'monotonic', return_value=20.0):
            raw = (b'[REC] SD records; select: l record select <number>\r\n'
                   b'[REC] 1 left_1 samples=100 duration_ms=2000\r\n'
                   b'[REC] count=1; delete: l record delete <number>; cancel: l record cancel\r\n')
            for byte in raw:
                self.logs.feed('left', bytes([byte]))
            self.logs.feed('left', b'TK,1,2,3\r\n')
        self.assertTrue(self.logs.records['left'].menu)
        self.assertEqual(self.logs.records['right'].entries, {})
        for side, marker in (('left', 'ROBOT1_JC_CH0_CH4'), ('right', 'ROBOT0_JB_CH0_CH4')):
            self.instance.handle_input(f'{side[0]} V')
            self.logs.feed(side, f'[PWM] enabled=0 arm={marker}\r\n'.encode('ascii'))
        shown = [self.logs.display.get_nowait() for _ in range(self.logs.display.qsize())]
        self.assertIn('[L] === 재생할 레코드 선택 ===', shown)
        self.assertIn('[L] 1. left_1', shown)
        self.assertIn('[L] [PWM] enabled=0 arm=ROBOT1_JC_CH0_CH4', shown)
        self.assertIn('[R] [PWM] enabled=0 arm=ROBOT0_JB_CH0_CH4', shown)
        self.assertFalse(any('TK,' in line for line in shown))
        with self.logs.lock:
            self.logs.flush_locked()
        self.assertEqual((self.output / 'left_uart.raw').read_bytes(),
                         raw + b'TK,1,2,3\r\n[PWM] enabled=0 arm=ROBOT1_JC_CH0_CH4\r\n')
        combined = (self.output / 'combined_uart.log').read_text(encoding='utf-8')
        self.assertIn('[L] [REC] SD records; select: l record select', combined)
        self.assertIn('[R] [PWM] enabled=0 arm=ROBOT0_JB_CH0_CH4', combined)

    def test_left_number_twice_waits_for_left_play_ack(self):
        self.menu('left')
        self.instance.handle_menu_key('1')
        self.instance.handle_menu_key('1')
        self.assertEqual(self.ports[0].writes, [b'!REC,SELECT,1\r'])
        self.assertEqual(self.ports[1].writes, [])
        self.logs.feed('right', b'[REC] PLAY id=1 name=right_1 samples=100 repeat=1; r P stops\n')
        self.assertTrue(self.logs.records['left'].menu)
        self.assertIsNotNone(self.logs.records['left'].select_pending)
        self.logs.feed('left', b'[REC] PLAY id=1 name=left_1 samples=100 repeat=1; l P stops\n')
        self.assertFalse(self.logs.records['left'].menu)
        self.assertEqual(self.logs.records['left'].playing, (1, 'left_1'))
        shown = [self.logs.display.get_nowait() for _ in range(self.logs.display.qsize())]
        self.assertTrue(any(line.startswith('[L] 재생 시작: 1. left_1') and '중지: l P' in line
                            for line in shown))

    def test_pending_left_menu_does_not_redirect_keys_to_existing_right_menu(self):
        self.menu('right')
        self.instance.handle_input('l P')
        self.assertFalse(self.instance.handle_menu_key('1'))
        self.assertEqual(self.ports[1].writes, [])
        self.menu('left')
        self.instance.handle_menu_key('1')
        self.instance.handle_menu_key('1')
        self.assertEqual(self.ports[0].writes, [b'P', b'!REC,SELECT,1\r'])
        self.assertTrue(self.logs.records['right'].menu)
        self.assertIsNone(self.logs.records['right'].select_pending)

    def test_simultaneous_menus_keep_selections_on_their_board(self):
        self.menu('left')
        self.menu('right')
        self.assertFalse(self.instance.handle_menu_key('1'))
        self.instance.handle_input('1')
        self.assertEqual([port.writes for port in self.ports], [[], []])
        self.assertEqual(self.logs.commands[-1]['status'], 'rejected')
        self.instance.handle_input('l record list')
        self.instance.handle_menu_key('1')
        self.instance.handle_input('r record list')
        self.instance.handle_menu_key('1')
        self.assertEqual([port.writes for port in self.ports], [[b'!REC,LIST\r'], [b'!REC,LIST\r']])
        self.instance.handle_menu_key('1')
        self.instance.handle_input('l record list')
        self.instance.handle_menu_key('1')
        self.assertEqual(self.ports[0].writes, [b'!REC,LIST\r', b'!REC,LIST\r', b'!REC,SELECT,1\r'])
        self.assertEqual(self.ports[1].writes, [b'!REC,LIST\r', b'!REC,SELECT,1\r'])
        self.logs.feed('left', b'[REC] PLAY id=1 name=left_1 samples=100 repeat=1\n')
        self.assertFalse(self.logs.records['left'].menu)
        self.assertTrue(self.logs.records['right'].menu)
        self.assertIsNotNone(self.logs.records['right'].select_pending)
        self.logs.feed('right', b'[REC] select rejected IO reason=NONE; pose held\n')
        self.assertIsNone(self.logs.records['right'].select_pending)
        self.assertEqual(self.logs.records['left'].playing, (1, 'left_1'))

    def test_delete_confirmation_and_deleted_names_are_per_side(self):
        self.menu('left')
        self.menu('right')
        for side in monitor.SIDES:
            self.instance.handle_menu_key('1', side)
            self.instance.handle_menu_key('\x08', side)
        self.logs.feed('right', b'[REC] confirm deleting 1 right_1: r record confirm 1; cancel: r record cancel\n')
        self.instance.handle_menu_key('\r', 'left')
        self.assertEqual(self.ports[0].writes, [b'!REC,DELETE,1\r'])
        self.logs.feed('left', b'[REC] confirm deleting 1 left_1: r record confirm 1\n')
        self.instance.handle_menu_key('\r', 'left')
        self.assertEqual(self.ports[0].writes, [b'!REC,DELETE,1\r'])
        self.logs.feed('left', b'[REC] confirm deleting 1 left_1: l record confirm 1; cancel: l record cancel\n')
        self.instance.handle_menu_key('\r', 'left')
        self.logs.feed('left', b'[REC] DELETED id=1\n')
        self.assertEqual(self.ports[0].writes, [b'!REC,DELETE,1\r', b'!REC,CONFIRM,1\r'])
        self.assertEqual(self.logs.records['right'].delete_confirm, 1)
        self.assertEqual(self.logs.records['right'].entries, {1: 'right_1'})
        self.instance.handle_menu_key('\r', 'right')
        self.logs.feed('right', b'[REC] DELETED id=1\n')
        self.assertEqual(self.logs.records['left'].last_deleted, (1, 'left_1'))
        self.assertEqual(self.logs.records['right'].last_deleted, (1, 'right_1'))
        shown = [self.logs.display.get_nowait() for _ in range(self.logs.display.qsize())]
        self.assertIn('[L] 삭제 완료: 1. left_1 / 남은 목록: l P', shown)
        self.assertIn('[R] 삭제 완료: 1. right_1 / 남은 목록: r P', shown)

    def test_name_prompts_do_not_steal_each_others_input(self):
        self.logs.feed('left', b'[REC] enter name: l record name <name>\n')
        self.logs.feed('right', b'[REC] enter name: r record name <name>\n')
        self.instance.handle_input('ambiguous_name')
        self.assertEqual([port.writes for port in self.ports], [[], []])
        self.instance.handle_input('l record name left_1')
        self.instance.handle_input('left_retry')
        self.assertEqual(self.ports[0].writes, [b'!REC,NAME,left_1\r', b'!REC,NAME,left_retry\r'])
        self.logs.feed('left', b'[REC] SAVED id=1 name=left_retry samples=100\n')
        self.instance.handle_input('still_ambiguous')
        self.assertEqual(self.ports[1].writes, [])
        self.instance.handle_input('r record name right_1')
        self.assertEqual(self.ports[1].writes, [b'!REC,NAME,right_1\r'])
        self.assertTrue(self.logs.records['right'].name_prompt)
        self.assertFalse(self.logs.records['left'].name_prompt)

    def test_single_left_name_and_number_use_left_port(self):
        self.logs.feed('left', b'[REC] enter name: l record name <name>\n')
        self.instance.handle_input('left_1')
        self.assertEqual(self.ports[0].writes, [b'!REC,NAME,left_1\r'])
        self.logs.feed('left', b'[REC] SAVED id=1 name=left_1 samples=100\n')
        self.menu('left')
        self.instance.handle_input('1')
        self.assertEqual(self.ports[0].writes[-1], b'!REC,SELECT,1\r')
        self.assertEqual(self.ports[1].writes, [])

    def test_pending_selection_blocks_only_same_side_delete(self):
        self.menu('left')
        self.menu('right')
        self.instance.handle_input('l record select 1')
        self.instance.handle_input('l record delete 1')
        self.instance.handle_input('l record confirm 1')
        self.instance.handle_input('r record delete 1')
        self.assertEqual(self.ports[0].writes, [b'!REC,SELECT,1\r'])
        self.assertEqual(self.ports[1].writes, [b'!REC,DELETE,1\r'])
        self.assertEqual([event['status'] for event in self.logs.commands],
                         ['sent', 'blocked', 'blocked', 'sent'])

    def test_pending_timeouts_and_boot_resets_are_per_side(self):
        self.menu('left')
        self.menu('right')
        with patch.object(monitor.time, 'monotonic', return_value=10.0):
            self.instance.handle_input('l record select 1')
        with patch.object(monitor.time, 'monotonic', return_value=12.0):
            self.instance.handle_input('r record select 1')
        with patch.object(monitor.time, 'monotonic', return_value=15.0):
            self.instance.poll_record_selection()
            self.instance.poll_record_selection()
        self.assertIsNone(self.logs.records['left'].select_pending)
        self.assertIsNotNone(self.logs.records['right'].select_pending)
        timeouts = [event for event in self.logs.commands if event['status'] == 'response_timeout']
        self.assertEqual([event['target'] for event in timeouts], ['l'])
        self.assertIn('l V/l P', timeouts[0]['error'])
        self.logs.feed('left', b' Zybo Z7-20 CNN camera platform, 720p60\n')
        self.assertEqual(self.logs.records['left'].entries, {})
        self.assertIsNotNone(self.logs.records['right'].select_pending)
        self.logs.feed('right', b'[REC] PLAY id=1 name=right_1 samples=100 repeat=1\n')
        self.assertIsNone(self.logs.records['right'].select_pending)
        self.assertEqual([port.writes for port in self.ports], [[b'!REC,SELECT,1\r']] * 2)

    def test_left_cancel_and_stop_leave_right_menu_intact(self):
        self.menu('left')
        self.menu('right')
        self.instance.handle_menu_key('\x1b', 'left')
        self.assertEqual(self.ports[0].writes, [b'!REC,CANCEL\r'])
        self.assertTrue(self.logs.records['right'].menu)
        self.menu('left')
        self.instance.handle_menu_key('1', 'left')
        self.instance.handle_input('l S')
        self.assertIsNone(self.logs.records['left'].selection)
        self.assertFalse(self.logs.records['left'].menu)
        self.assertTrue(self.logs.records['right'].menu)
        self.assertEqual(self.ports[1].writes, [])

    def test_left_motion_gate_keeps_menu_and_never_transmits(self):
        self.instance.allow_motion_commands = False
        self.menu('left')
        self.instance.handle_menu_key('1')
        self.instance.handle_menu_key('1')
        self.instance.handle_input('l R')
        self.instance.handle_input('l E')
        self.instance.handle_input('l A')
        self.assertEqual([port.writes for port in self.ports], [[], []])
        self.assertTrue(self.logs.records['left'].menu)
        self.assertIsNone(self.logs.records['left'].select_pending)
        self.assertTrue(all(event['status'] == 'blocked' for event in self.logs.commands))

    def test_left_short_write_clears_only_left_pending_selection(self):
        self.menu('left')
        self.menu('right')
        self.instance.handle_input('r record select 1')
        self.ports[0].write_result = 0
        self.instance.handle_input('l record select 1')
        self.assertIsNone(self.logs.records['left'].select_pending)
        self.assertIsNotNone(self.logs.records['right'].select_pending)
        self.assertTrue(self.logs.records['left'].menu)
        self.assertEqual(self.ports[0].writes, [b'!REC,SELECT,1\r'])
        self.assertEqual(self.ports[1].writes, [b'!REC,SELECT,1\r'])
        self.assertEqual(self.logs.commands[-1]['target'], 'l')
        self.assertEqual(self.logs.commands[-1]['status'], 'failed')

    def test_queued_keyboard_keys_keep_captured_side(self):
        import msvcrt

        self.menu('left')
        commands = queue.Queue()
        keys = iter('1\x03')
        with patch.object(msvcrt, 'kbhit', return_value=True), \
                patch.object(msvcrt, 'getwch', side_effect=lambda: next(keys)):
            monitor.read_windows_console(commands, self.instance.stop, self.instance)
        item = commands.get_nowait()
        self.assertEqual(item, ('key', '1', 'left'))
        self.menu('right')
        self.instance.record_side = 'right'
        self.instance.input_queue.put(item)
        self.instance.process_input()
        self.assertEqual(self.logs.records['left'].selection, 1)
        self.assertIsNone(self.logs.records['right'].selection)
        self.assertEqual([port.writes for port in self.ports], [[], []])


class RecordStatusMonitorTests(unittest.TestCase):
    def setUp(self):
        self.output = Path(self.enterContext(tempfile.TemporaryDirectory()))
        self.screen = io.StringIO()
        self.enterContext(redirect_stdout(self.screen))
        self.logs = monitor.MonitorLogs(self.output, 'commands', display_format='plain')
        self.addCleanup(self.logs.close)
        self.ports = [FakeWritablePort(), FakeWritablePort()]
        self.instance = monitor.ConsoleMonitor(self.ports, self.logs, True, True)

    def state(self, side='right', ui='NAME', mode='HOLDING', pwm=1,
              ram_samples=100, entries=1, selected=0, playing=0, delete=0, unsaved=1):
        raw = (f'[REC_STATE] schema=1 side={side[0]} ui={ui} mode={mode} pwm={pwm} '
               f'ram_samples={ram_samples} entries={entries} selected={selected} '
               f'playing={playing} delete={delete} unsaved={unsaved}\r\n').encode('ascii')
        self.logs.feed(side, raw)
        return raw

    def test_restart_restores_fragmented_name_state_without_query(self):
        raw = (b'[REC_STATE] schema=1 side=l ui=NAME mode=HOLDING pwm=0 '
               b'ram_samples=123 entries=2 selected=3 playing=0 delete=0 unsaved=1\r\n')
        for byte in raw:
            self.logs.feed('left', bytes([byte]))
        record = self.logs.records['left']
        self.assertTrue(record.name_prompt)
        self.assertFalse(record.menu)
        self.assertEqual((record.ui, record.mode, record.pwm), ('NAME', 'HOLDING', False))
        self.assertEqual((record.ram_samples, record.entries_count, record.selected_id), (123, 2, 3))
        self.assertTrue(record.unsaved)
        self.assertEqual([port.writes for port in self.ports], [[], []])
        shown = self.logs.display.get_nowait()
        self.assertIn('RAM 123개 / SD 미저장', shown)
        self.assertIn('저장할 이름 입력', shown)
        self.instance.handle_input('restarted_name')
        self.assertEqual([port.writes for port in self.ports], [[b'!REC,NAME,restarted_name\r'], []])
        with self.logs.lock:
            self.logs.flush_locked()
        self.assertEqual((self.output / 'left_uart.raw').read_bytes(), raw)
        self.assertIn(raw.decode().strip(), (self.output / 'left_uart.log').read_text())

    def test_legacy_v_restores_name_on_each_board_without_query(self):
        for side in monitor.SIDES:
            self.logs.feed(side, b'[REC] entries=2 selected=3 ui=3; record list\n')
            record = self.logs.records[side]
            self.assertTrue(record.name_prompt)
            self.assertFalse(record.menu)
            self.assertEqual((record.ui, record.entries_count, record.selected_id), ('NAME', 2, 3))
            self.assertEqual(self.ports[monitor.SIDES.index(side)].writes, [])
            self.instance.handle_input(f'{side[0]} record name {side}_1')
        self.assertEqual([port.writes for port in self.ports],
                         [[b'!REC,NAME,left_1\r'], [b'!REC,NAME,right_1\r']])

    def test_all_legacy_numeric_ui_states_clear_stale_pending(self):
        record = self.logs.records['right']
        for numeric, ui in enumerate(('IDLE', 'STOP_RECORD', 'STOP_MENU', 'NAME',
                                      'MENU', 'SAVE', 'START_RECORD')):
            with self.subTest(ui=ui):
                record.select_pending = {'id': 1, 'until': 1e20}
                record.selection = 1
                record.delete_confirm = 1
                record.delete_requested = True
                self.logs.feed('right', f'[REC] entries=1 selected=1 ui={numeric}\n'.encode())
                self.assertEqual(record.ui, ui)
                self.assertEqual(record.name_prompt, ui in ('NAME', 'SAVE'))
                self.assertEqual(record.menu, ui == 'MENU')
                self.assertIsNone(record.select_pending)
                self.assertIsNone(record.delete_confirm)
                self.assertIsNone(record.selection)
                self.assertFalse(record.delete_requested)
        self.assertEqual([port.writes for port in self.ports], [[], []])

    def test_s_x_and_cancel_request_keep_name_until_board_confirms(self):
        for side in monitor.SIDES:
            self.state(side=side)
        for key in ('S', 'X'):
            self.instance.handle_input(f'both {key}')
            for side in monitor.SIDES:
                self.assertTrue(self.logs.records[side].name_prompt)
                self.assertTrue(self.logs.records[side].unsaved)
                self.state(side=side, pwm=0)
                self.assertTrue(self.logs.records[side].name_prompt)
        self.instance.handle_input('l record cancel')
        self.assertTrue(self.logs.records['left'].name_prompt)
        self.logs.feed('left', b'[REC] rejected SAVE_BUSY\n')
        self.assertTrue(self.logs.records['left'].name_prompt)
        self.logs.feed('left', b'[REC] cancelled; unsaved RAM may be replaced by next recording\n')
        self.assertFalse(self.logs.records['left'].name_prompt)
        self.assertTrue(self.logs.records['left'].unsaved)
        self.assertTrue(self.logs.records['right'].name_prompt)
        self.assertEqual([port.writes for port in self.ports],
                         [[b'\x1bS', b'\x1bX', b'!REC,CANCEL\r'], [b'\x1bS', b'\x1bX']])

    def test_ui_busy_reconciles_name_and_list_cannot_open_fake_menu(self):
        record = self.logs.records['right']
        record.entries = {1: 'old_record'}
        record.menu = True
        self.instance.handle_input('r record select 1')
        self.logs.feed('right', b'[REC] rejected UI_BUSY; finish naming or r record cancel\n')
        self.assertFalse(record.menu)
        self.assertIsNone(record.select_pending)
        self.logs.feed('right', b'[REC] entries=1 selected=1 ui=3; r record list\n')
        self.assertTrue(record.name_prompt)
        self.logs.feed('right', b'[REC] SD records; select: r record select <number>\n'
                       b'[REC] 1 old_record samples=100 duration_ms=2000\n'
                       b'[REC] count=1; cancel: r record cancel\n')
        self.assertFalse(record.menu)
        self.assertFalse(self.instance.handle_menu_key('1', 'right'))
        self.instance.handle_input('new_name')
        self.assertEqual(self.ports[1].writes, [b'!REC,SELECT,1\r', b'!REC,NAME,new_name\r'])
        self.assertEqual(self.ports[0].writes, [])

    def test_rejected_name_keeps_ram_and_can_retry(self):
        self.state()
        self.instance.handle_input('duplicate')
        self.logs.feed('right', b'[REC] name rejected NAME_STATE_DUPLICATE_FULL_OR_IO\n')
        record = self.logs.records['right']
        self.assertTrue(record.name_prompt)
        self.assertTrue(record.unsaved)
        self.assertEqual(record.ram_samples, 100)
        self.assertFalse(record.menu)
        self.instance.handle_input('retry_name')
        self.assertEqual(self.ports[1].writes,
                         [b'!REC,NAME,duplicate\r', b'!REC,NAME,retry_name\r'])
        shown = [self.logs.display.get_nowait() for entry in range(self.logs.display.qsize())]
        self.assertTrue(any('RAM 기록 SD 미저장 상태 유지' in line for line in shown))

    def test_save_busy_and_failure_keep_name_until_saved(self):
        self.state()
        self.instance.handle_input('save_name')
        self.state(ui='SAVE')
        self.logs.feed('right', b'[REC] rejected SAVE_BUSY\n')
        self.assertTrue(self.logs.records['right'].name_prompt)
        self.assertTrue(self.logs.records['right'].unsaved)
        self.logs.feed('right', b'[REC] SAVE_FAILED; RAM retained, old files unchanged\n')
        self.assertEqual(self.logs.records['right'].ui, 'NAME')
        self.instance.handle_input('save_retry')
        self.logs.feed('right', b'[REC] SAVED id=2 name=save_retry samples=100\n')
        self.assertFalse(self.logs.records['right'].name_prompt)
        self.assertFalse(self.logs.records['right'].unsaved)
        self.assertEqual(self.ports[1].writes,
                         [b'!REC,NAME,save_name\r', b'!REC,NAME,save_retry\r'])

    def test_status_cancellation_keeps_visible_unsaved_ram(self):
        self.state()
        self.instance.handle_input('r record cancel')
        self.state(ui='IDLE', mode='LIVE')
        record = self.logs.records['right']
        self.assertFalse(record.name_prompt)
        self.assertFalse(record.menu)
        self.assertTrue(record.unsaved)
        self.assertEqual(record.ram_samples, 100)
        shown = [self.logs.display.get_nowait() for entry in range(self.logs.display.qsize())]
        self.assertIn('RAM 100개 / SD 미저장', shown[-1])
        self.assertNotIn('저장할 이름 입력', shown[-1])
        self.assertEqual(self.ports[1].writes, [b'!REC,CANCEL\r'])

    def test_authoritative_playback_clears_pending_and_stale_delete(self):
        record = self.logs.records['right']
        record.entries = {3: 'known_record'}
        record.select_pending = {'id': 3, 'until': 1e20}
        record.selection = 3
        record.delete_confirm = 3
        record.delete_requested = True
        self.state(ui='IDLE', mode='ALIGNING', selected=3, playing=3, unsaved=0)
        self.assertEqual(record.playing, (3, 'known_record'))
        self.assertIsNone(record.select_pending)
        self.assertIsNone(record.selection)
        self.assertIsNone(record.delete_confirm)
        self.assertFalse(record.delete_requested)
        self.assertFalse(record.menu)
        self.state(ui='IDLE', mode='PLAYING', selected=3, playing=3, unsaved=0)
        self.assertEqual(record.playing, (3, 'known_record'))
        self.state(ui='IDLE', mode='HOLDING', entries=0, unsaved=0)
        self.assertIsNone(record.playing)
        self.assertEqual(record.entries, {})
        self.assertEqual(record.selected_id, 0)
        self.assertEqual([port.writes for port in self.ports], [[], []])

    def test_restart_delete_confirmation_works_without_catalog(self):
        for ui in ('MENU', 'IDLE'):
            with self.subTest(ui=ui):
                self.state(ui=ui, selected=3, delete=3, unsaved=0)
                record = self.logs.records['right']
                self.assertTrue(record.menu)
                self.assertEqual(record.selection, 3)
                self.assertEqual(record.delete_confirm, 3)
                self.assertTrue(record.delete_requested)
                self.instance.handle_menu_key('\r', 'right')
                self.assertEqual(self.ports[1].writes[-1], b'!REC,CONFIRM,3\r')
                self.state(ui=ui, selected=0, delete=0, unsaved=0)
                self.assertIsNone(record.delete_confirm)
                self.assertIsNone(record.selection)
                self.assertFalse(record.delete_requested)
        self.assertEqual(self.ports[1].writes, [b'!REC,CONFIRM,3\r'] * 2)
        self.assertEqual(self.ports[0].writes, [])

    def test_menu_status_preserves_two_key_selection_and_settles_request(self):
        self.logs.records['right'].entries = {1: 'record_1'}
        self.state(ui='MENU', unsaved=0)
        self.instance.handle_menu_key('1', 'right')
        self.state(ui='MENU', unsaved=0)
        self.assertEqual(self.logs.records['right'].selection, 1)
        self.assertEqual(self.ports[1].writes, [])
        self.instance.handle_menu_key('1', 'right')
        self.assertIsNotNone(self.logs.records['right'].select_pending)
        self.state(ui='MENU', unsaved=0)
        self.assertIsNone(self.logs.records['right'].select_pending)
        self.assertTrue(self.logs.records['right'].menu)
        self.instance.handle_menu_key('1', 'right')
        self.instance.handle_menu_key('1', 'right')
        self.state()
        self.assertTrue(self.logs.records['right'].name_prompt)
        self.assertFalse(self.logs.records['right'].menu)
        self.assertIsNone(self.logs.records['right'].select_pending)
        self.assertEqual(self.ports[1].writes, [b'!REC,SELECT,1\r'] * 2)

    def test_timeout_uses_confirmed_name_state_instead_of_cached_entries(self):
        self.state()
        record = self.logs.records['right']
        record.entries = {1: 'old_record'}
        record.select_pending = {'id': 1, 'until': 0}
        self.instance.poll_record_selection()
        self.assertFalse(record.menu)
        self.assertTrue(record.name_prompt)
        self.assertIsNone(record.select_pending)
        self.assertEqual([port.writes for port in self.ports], [[], []])

    def test_immediate_status_is_not_overwritten_by_keyboard_cleanup(self):
        record = self.logs.records['right']
        record.entries = {1: 'record_1', 3: 'record_3'}
        original_write = self.ports[1].write

        def reply(payload):
            written = original_write(payload)
            self.logs.feed('right', b'[REC] rejected FRAME_OR_MENU_OR_BUSY\n')
            self.state(ui='MENU', entries=2, selected=3, delete=3, unsaved=0)
            return written

        for operation in ('select', 'confirm', 'cancel'):
            with self.subTest(operation=operation):
                self.state(ui='MENU', entries=2, delete=0 if operation == 'select' else 3,
                           unsaved=0)
                with patch.object(self.ports[1], 'write', side_effect=reply):
                    if operation == 'select':
                        self.instance.handle_menu_key('1', 'right')
                        self.instance.handle_menu_key('1', 'right')
                    else:
                        self.instance.handle_menu_key('\r' if operation == 'confirm' else '\x1b', 'right')
                self.assertTrue(record.menu)
                self.assertEqual(record.delete_confirm, 3)
                self.assertEqual(record.selection, 3)
                self.assertTrue(record.delete_requested)
                self.assertIsNone(record.select_pending)
        self.assertEqual(self.ports[1].writes,
                         [b'!REC,SELECT,1\r', b'!REC,CONFIRM,3\r', b'!REC,CANCEL\r'])
        self.assertEqual(self.ports[0].writes, [])

    def test_new_schema_overrides_legacy_until_board_reboot(self):
        self.state(ui='MENU', selected=3, delete=3, unsaved=0)
        self.logs.feed('right', b'[REC] entries=0 selected=0 ui=3; r record list\n')
        record = self.logs.records['right']
        self.assertEqual((record.ui, record.selected_id, record.delete_confirm), ('MENU', 3, 3))
        self.assertFalse(record.name_prompt)
        self.assertFalse(record.unsaved)
        self.logs.feed('right', b' Zybo Z7-20 CNN camera platform, 720p60\n')
        record = self.logs.records['right']
        self.assertIsNone(record.schema)
        self.assertIsNone(record.delete_confirm)
        self.assertIsNone(record.ram_samples)
        self.assertFalse(record.name_prompt)
        self.logs.feed('right', b'[REC] entries=1 selected=1 ui=3\n')
        self.assertTrue(record.name_prompt)
        self.assertTrue(record.unsaved)
        self.assertEqual([port.writes for port in self.ports], [[], []])

    def test_wrong_side_unknown_schema_and_partial_status_do_not_mutate_state(self):
        raw = self.state()
        record = self.logs.records['right']
        expected = dict(vars(record))
        for invalid in (raw.replace(b'schema=1', b'schema=2'),
                        raw.replace(b'side=r', b'side=l'),
                        raw.replace(b'ui=NAME', b'ui=UNKNOWN'),
                        raw.replace(b'mode=HOLDING', b'mode=UNKNOWN'),
                        raw.replace(b'pwm=1', b'pwm=2'),
                        raw.replace(b' delete=0', b'')):
            with self.subTest(invalid=invalid):
                self.logs.feed('right', invalid)
                self.assertEqual(vars(record), expected)
        self.assertEqual([port.writes for port in self.ports], [[], []])

    def test_name_menu_delete_and_pending_are_independent_per_board(self):
        self.state(side='left')
        self.state(side='right', ui='MENU', delete=3, unsaved=0)
        self.assertTrue(self.logs.records['left'].name_prompt)
        self.assertFalse(self.logs.records['right'].name_prompt)
        self.instance.handle_input('l record name left_save')
        self.instance.handle_menu_key('\r', 'right')
        self.state(side='right', ui='IDLE', mode='PLAYING', selected=3, playing=3, unsaved=0)
        self.assertTrue(self.logs.records['left'].name_prompt)
        self.assertTrue(self.logs.records['left'].unsaved)
        self.assertEqual(self.logs.records['right'].playing, (3, ''))
        self.logs.feed('right', b'Zybo Z7-20 CNN camera platform, 720p60\n')
        self.assertTrue(self.logs.records['left'].name_prompt)
        self.assertIsNone(self.logs.records['right'].playing)
        self.assertEqual([port.writes for port in self.ports],
                         [[b'!REC,NAME,left_save\r'], [b'!REC,CONFIRM,3\r']])


if __name__ == '__main__':
    unittest.main()
