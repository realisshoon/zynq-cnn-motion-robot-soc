"""Log both board consoles; optionally send keys and decimal submenu values."""

import argparse
from contextlib import ExitStack
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import queue
import sys
import threading
import time


SIDES = ('left', 'right')
LABELS = {'left': 'L', 'right': 'R'}
MAX_LINE_BYTES = 65536
COMMAND_TARGETS = {'l': ('left',), 'r': ('right',), 'both': SIDES}
MOTION_COMMANDS = frozenset('RPEXAufjhik')
MENU_PROMPT_END = b'Enter=keep]: '
VERBOSE_TRACE_TAGS = frozenset(('A1', 'P3', 'A2', 'TK', 'SM', 'CN', 'CAM', 'IN', 'CS',
                                'RAW', 'PAIR', 'PIX', 'PG', 'RQ'))


def parse_command(text):
    text = text.removesuffix('\n').removesuffix('\r')
    if any(character != '\t' and not 32 <= ord(character) <= 126
           for character in text):
        raise ValueError('Use ASCII only: l/r/both <key>, l/r/both value <digits>, '
                         'l/r/both enter, or q')
    parts = text.split()
    if not parts:
        return None
    if parts == ['q']:
        return ('exit', None)
    if len(parts) == 2 and parts[0] in COMMAND_TARGETS and parts[1] == 'enter':
        return (parts[0], '\r')
    if (len(parts) == 3 and parts[0] in COMMAND_TARGETS and parts[1] == 'value'
            and 1 <= len(parts[2]) <= 6 and parts[2].isascii() and parts[2].isdigit()):
        return (parts[0], parts[2] + '\r')
    if (len(parts) != 2 or parts[0] not in COMMAND_TARGETS or
            len(parts[1]) != 1 or not 33 <= ord(parts[1]) <= 126):
        raise ValueError('Expected l/r/both <key>, l/r/both value <1..6 decimal digits>, '
                         'or l/r/both enter')
    return tuple(parts)


class MonitorLogs:
    def __init__(self, output, display_filter='all', queue_size=4096,
                 display_format='tagged', response_window=3.0):
        self.stack = ExitStack()
        self.lock = threading.Lock()
        self.display_filter = display_filter
        self.display_format = display_format
        self.response_window = response_window
        self.response_windows = {side: None for side in SIDES}
        self.response_serial = 0
        self.line_response_ids = {side: None for side in SIDES}
        self.display = queue.Queue(maxsize=queue_size)
        self.buffers = {side: bytearray() for side in SIDES}
        self.prompt_offsets = {side: 0 for side in SIDES}
        self.stats = {side: {'bytes': 0, 'lines': 0, 'display_dropped': 0,
                             'split_lines': 0} for side in SIDES}
        self.last_flush = time.monotonic()
        self.commands = []
        try:
            self.raw = {side: self.stack.enter_context(
                (output / f'{side}_uart.raw').open('wb')) for side in SIDES}
            self.logs = {side: self.stack.enter_context(
                (output / f'{side}_uart.log').open('w', encoding='utf-8', newline='\n'))
                for side in SIDES}
            self.combined = self.stack.enter_context(
                (output / 'combined_uart.log').open('w', encoding='utf-8', newline='\n'))
            self.audit = self.stack.enter_context(
                (output / 'host_commands.jsonl').open('w', encoding='utf-8', newline='\n'))
        except BaseException:
            self.stack.close()
            raise

    def record_command(self, event):
        event = {'timestamp_utc': datetime.now(timezone.utc).isoformat(
            timespec='milliseconds'), **event}
        with self.lock:
            self.audit.write(json.dumps(event, ensure_ascii=True) + '\n')
            self.audit.flush()
            self.commands.append(event)
        if self.display_format == 'plain':
            entered = event.get('input', '').encode('unicode_escape').decode('ascii')
            details = ', '.join(
                f'{LABELS[result["side"]]} {result["bytes_written"]}/'
                f'{result["bytes_requested"]} {result["status"]}'
                for result in event.get('results', []))
            error = event.get('error') or '; '.join(
                result['error'] for result in event.get('results', []) if 'error' in result)
            print(f'[HOST] {entered or "stdin"}: {event["status"]}'
                  + (f' ({details})' if details else '')
                  + (f'; {error}' if error else ''), flush=True)
        else:
            print('[HOST] command ' + json.dumps(event, ensure_ascii=True), flush=True)
        if event.get('command') == 'q' and event['status'] in ('sent', 'partial'):
            print('[HOST] q toggles BOARD UART output; muting may have no reply. '
                  'CNN/PWM/control keep running.', flush=True)

    def arm_response(self, side, command):
        if self.display_filter != 'commands':
            return
        with self.lock:
            self.response_serial += 1
            self.response_windows[side] = {
                'id': self.response_serial, 'command': command,
                'until': time.monotonic() + self.response_window}

    def cancel_response(self, side):
        with self.lock:
            self.response_windows[side] = None

    def active_response_locked(self, side):
        window = self.response_windows[side]
        return window if window and time.monotonic() < window['until'] else None

    def visible_locked(self, side, line, response_id=None):
        tag = line.lstrip().removeprefix('#').partition(',')[0]
        if self.display_filter == 'commands':
            window = self.active_response_locked(side)
            selected_id = self.line_response_ids[side] if response_id is None else response_id
            if not window or selected_id != window['id']:
                return False
            if tag in VERBOSE_TRACE_TAGS:
                return False
            if line.lstrip().startswith('[ST]'):
                return (window['command'] in ('A', 'S', 'T', 'X')
                        and line.lstrip().startswith('[ST] async_test=')
                        and ' pwm=' in line and ' result=' in line)
            return bool(line.strip())
        return (self.display_filter == 'all' or '[ST]' in line or
                MENU_PROMPT_END.decode('ascii') in line or
                (self.display_filter == 'console' and bool(line.strip()) and
                 tag not in VERBOSE_TRACE_TAGS))

    def display_locked(self, side, line, timestamp):
        try:
            prefix = f'{timestamp} ' if self.display_format == 'tagged' else ''
            self.display.put_nowait(f'{prefix}[{LABELS[side]}] {line}')
        except queue.Full:
            self.stats[side]['display_dropped'] += 1

    def emit_locked(self, side, raw_line, timestamp, suffix='', display_start=0):
        line = raw_line.decode('utf-8', errors='replace') + suffix
        tagged = f'{timestamp} [{LABELS[side]}] {line}'
        self.logs[side].write(line + '\n')
        self.combined.write(tagged + '\n')
        self.stats[side]['lines'] += 1
        remaining = raw_line[display_start:].decode('utf-8', errors='replace') + suffix
        if self.visible_locked(side, remaining):
            if remaining or not display_start:
                self.display_locked(side, remaining, timestamp)

    def flush_locked(self):
        for log in (*self.raw.values(), *self.logs.values(), self.combined, self.audit):
            log.flush()
        self.last_flush = time.monotonic()

    def feed(self, side, data):
        timestamp = datetime.now(timezone.utc).isoformat(timespec='milliseconds')
        with self.lock:
            self.raw[side].write(data)
            self.stats[side]['bytes'] += len(data)
            buffer = self.buffers[side]
            window = self.active_response_locked(side)
            response_id = window['id'] if window else None
            if not buffer:
                self.line_response_ids[side] = response_id
            buffer.extend(data)
            while buffer:
                newline = buffer.find(b'\n')
                if 0 <= newline < MAX_LINE_BYTES:
                    line = bytes(buffer[:newline]).removesuffix(b'\r')
                    del buffer[:newline + 1]
                    self.emit_locked(side, line, timestamp,
                                     display_start=self.prompt_offsets[side])
                    self.prompt_offsets[side] = 0
                    self.line_response_ids[side] = response_id
                elif len(buffer) >= MAX_LINE_BYTES:
                    line = bytes(buffer[:MAX_LINE_BYTES])
                    del buffer[:MAX_LINE_BYTES]
                    self.stats[side]['split_lines'] += 1
                    self.emit_locked(side, line, timestamp,
                                     ' [HOST: overlong line split]', self.prompt_offsets[side])
                    self.prompt_offsets[side] = 0
                    self.line_response_ids[side] = response_id
                else:
                    break
            prompt_position = buffer.rfind(MENU_PROMPT_END)
            if prompt_position >= 0:
                prompt_end = prompt_position + len(MENU_PROMPT_END)
                if prompt_end > self.prompt_offsets[side]:
                    line = bytes(buffer[self.prompt_offsets[side]:prompt_end]).decode(
                        'utf-8', errors='replace')
                    preview_id = self.line_response_ids[side]
                    if window and window['command'].endswith('\r') and self.prompt_offsets[side]:
                        preview_id = response_id
                    if self.visible_locked(side, line, preview_id):
                        self.display_locked(side, line, timestamp)
                    self.prompt_offsets[side] = prompt_end
            if time.monotonic() - self.last_flush >= 1.0:
                self.flush_locked()

    def flush_if_due(self):
        with self.lock:
            if time.monotonic() - self.last_flush >= 1.0:
                self.flush_locked()

    def close(self):
        try:
            with self.lock:
                timestamp = datetime.now(timezone.utc).isoformat(timespec='milliseconds')
                for side, buffer in self.buffers.items():
                    if buffer:
                        self.emit_locked(side, bytes(buffer), timestamp,
                                         ' [HOST: partial line at stop]', self.prompt_offsets[side])
                        buffer.clear()
                        self.prompt_offsets[side] = 0
                self.flush_locked()
        finally:
            self.stack.close()


class ConsoleMonitor:
    def __init__(self, ports, logs, interactive=False, allow_motion_commands=False):
        self.ports = ports
        self.logs = logs
        self.stop = threading.Event()
        self.errors = queue.Queue()
        self.threads = []
        self.interactive = interactive
        self.allow_motion_commands = allow_motion_commands
        self.input_queue = queue.Queue(maxsize=64)
        self.input_thread = None

    def handle_input(self, text):
        if not self.interactive or self.stop.is_set():
            return
        event = {'input': text.rstrip('\r\n'), 'target': None, 'command': None,
                 'results': []}
        try:
            parsed = parse_command(text)
        except ValueError as exc:
            self.logs.record_command({**event, 'status': 'rejected', 'error': str(exc)})
            return
        if parsed is None:
            return
        target, command = parsed
        event.update(target=target, command=command)
        if target == 'exit':
            self.logs.record_command({**event, 'status': 'exit'})
            self.stop.set()
            return
        if command in MOTION_COMMANDS and not self.allow_motion_commands:
            self.logs.record_command({**event, 'status': 'blocked', 'error':
                'Robot PWM/recording/replay or camera PWM/configuration key; motion risk. '
                'Requires --interactive --allow-motion-commands'})
            return
        if command in MOTION_COMMANDS:
            print(f'[HOST] WARNING: {command!r} can change robot PWM/live control/recording/replay '
                  'or camera PWM/configuration; motion may occur.', flush=True)
        if target == 'both' and (command == 'm' or command.endswith('\r')):
            print('[HOST] WARNING: BOTH submenu commands require BOTH boards at the same '
                  'menu field (or both ready to enter m). Check [L]/[R] prompts; '
                  'sequential, not atomic; no automatic retry.', flush=True)
        interrupted = False
        failed = False
        payload = command.encode('ascii')
        for side in COMMAND_TARGETS[target]:
            result = {'side': side, 'bytes_requested': len(payload), 'bytes_written': 0,
                      'status': 'not_attempted'}
            event['results'].append(result)
            if failed or self.stop.is_set():
                continue
            try:
                self.logs.arm_response(side, command)
                written = self.ports[SIDES.index(side)].write(payload)
                result.update(bytes_written=written, status='sent')
                if written != len(payload):
                    raise OSError(f'Short write: {written}/{len(payload)} bytes; no retry')
            except (Exception, KeyboardInterrupt) as exc:
                self.logs.cancel_response(side)
                if result['status'] == 'not_attempted':
                    result['bytes_written'] = None
                error = f'{type(exc).__name__}: {exc}'
                result.update(status='failed', error=error)
                self.errors.put(f'[{LABELS[side]}] TX {error}; delivery may be uncertain')
                self.stop.set()
                failed = True
                interrupted = isinstance(exc, KeyboardInterrupt)
        statuses = [result['status'] for result in event['results']]
        event['status'] = ('sent' if all(status == 'sent' for status in statuses) else
                           'partial' if 'sent' in statuses else 'failed')
        self.logs.record_command(event)
        if interrupted:
            raise KeyboardInterrupt

    def process_input(self):
        if not self.interactive or self.stop.is_set():
            return
        try:
            text = self.input_queue.get_nowait()
        except queue.Empty:
            return
        if text is None:
            self.logs.record_command({'status': 'eof', 'target': None,
                                      'command': None, 'results': []})
            self.stop.set()
        elif isinstance(text, Exception):
            error = f'stdin {type(text).__name__}: {text}'
            self.logs.record_command({'status': 'input_error', 'error': error,
                                      'target': None, 'command': None, 'results': []})
            self.errors.put(error)
            self.stop.set()
        else:
            self.handle_input(text)

    def read_board(self, side, port):
        try:
            while not self.stop.is_set():
                data = port.read(min(8192, max(1, port.in_waiting)))
                if data:
                    self.logs.feed(side, data)
        except Exception as exc:
            self.errors.put(f'[{LABELS[side]}] {type(exc).__name__}: {exc}')
            self.stop.set()

    def start(self):
        for side, port in zip(SIDES, self.ports):
            thread = threading.Thread(target=self.read_board, args=(side, port),
                                      name=f'uart-{side}')
            thread.start()
            self.threads.append(thread)
        if self.interactive:
            self.input_thread = threading.Thread(
                target=read_console_input, args=(sys.stdin, self.input_queue, self.stop),
                name='uart-console-input', daemon=True)
            self.input_thread.start()

    def close(self):
        self.stop.set()
        for thread in self.threads:
            thread.join()


def read_console_line(descriptor):
    data = bytearray()
    while True:
        byte = os.read(descriptor, 1)
        data.extend(byte)
        if not byte or byte == b'\n':
            return data.decode('ascii', errors='replace')


def read_console_input(stream, commands, stop):
    try:
        descriptor = stream.fileno()
    except (AttributeError, OSError, ValueError):
        descriptor = None
    while not stop.is_set():
        try:
            text = (stream.readline() if descriptor is None else
                    read_console_line(descriptor))
            item = text if text else None
        except Exception as exc:
            item = exc
        while not stop.is_set():
            try:
                commands.put(item, timeout=0.1)
                break
            except queue.Full:
                pass
        if item is None or isinstance(item, Exception):
            return


def open_port(serial_module, name, baud):
    port = serial_module.Serial(port=None, baudrate=baud, bytesize=8,
                                parity='N', stopbits=1, timeout=0.1, write_timeout=0.5,
                                xonxoff=False, rtscts=False, dsrdtr=False)
    try:
        port.dtr = False
        port.rts = False
        port.port = name
        port.open()
        return port
    except BaseException:
        port.close()
        raise


def display_logs(monitor):
    last_notice = time.monotonic()
    reported_drops = 0
    while not monitor.stop.is_set():
        monitor.process_input()
        if monitor.stop.is_set():
            break
        try:
            print(monitor.logs.display.get(timeout=0.1), flush=True)
        except queue.Empty:
            pass
        monitor.logs.flush_if_due()
        if time.monotonic() - last_notice >= 1.0:
            with monitor.logs.lock:
                dropped = sum(stats['display_dropped']
                              for stats in monitor.logs.stats.values())
            if dropped != reported_drops:
                print(f'[HOST] Display queue full: {dropped} lines skipped on screen; '
                      'disk logging continues. Prefer --filter stereo.', flush=True)
                reported_drops = dropped
            last_notice = time.monotonic()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--left', help='Left USB console port, e.g. COM3')
    parser.add_argument('--right', help='Right USB console port, e.g. COM4')
    parser.add_argument('--baud', type=int, default=921600)
    parser.add_argument('--filter', choices=('all', 'stereo', 'console', 'commands'), default='all',
                        help='Screen filter only; disk logs always retain all received data')
    parser.add_argument('--format', choices=('tagged', 'plain'), default='tagged',
                        help='Screen only: plain hides UTC labels and verbose host JSON')
    parser.add_argument('--response-window', type=float, default=3.0,
                        help='Commands filter reply window in seconds per sent target (default 3)')
    parser.add_argument('--output', type=Path, help='New log directory; never overwrite')
    parser.add_argument('--list-ports', action='store_true')
    parser.add_argument('--interactive', action='store_true',
                        help='Enable l/r/both <key>, l/r/both value <digits>, '
                             'l/r/both enter; bare q exits')
    parser.add_argument('--allow-motion-commands', action='store_true',
                        help='Allow E/X/A, R/P and camera PWM/configuration keys; motion risk')
    args = parser.parse_args()
    try:
        import serial
        from serial.tools import list_ports
    except ImportError:
        parser.error('Install pySerial: python -m pip install pyserial')
    if args.list_ports:
        ports = list(list_ports.comports())
        for port in ports:
            print(f'{port.device}: {port.description}')
        if not ports:
            print('No serial ports found.')
        return 0
    if (not args.left or not args.right or not args.left.strip() or not args.right.strip()
            or args.left.strip().upper() == args.right.strip().upper()):
        parser.error('--left and --right must be different, nonempty ports')
    if args.baud <= 0:
        parser.error('--baud must be positive')
    if args.allow_motion_commands and not args.interactive:
        parser.error('--allow-motion-commands requires --interactive')
    if args.filter == 'commands' and not args.interactive:
        parser.error('--filter commands requires --interactive')
    if not math.isfinite(args.response_window) or args.response_window <= 0:
        parser.error('--response-window must be finite and positive')

    output = args.output or Path('captures') / datetime.now().strftime(
        'monitor_%Y%m%d_%H%M%S_%f')
    output.mkdir(parents=True, exist_ok=False)
    session = {'started_utc': datetime.now(timezone.utc).isoformat(),
               'left_port': args.left, 'right_port': args.right, 'baud': args.baud,
               'screen_filter': args.filter, 'receive_only': not args.interactive,
               'screen_format': args.format, 'response_window_seconds': args.response_window,
               'interactive': args.interactive,
               'allow_motion_commands': args.allow_motion_commands, 'commands': [],
               'timestamp_source': 'host line receipt, NOT camera exposure',
               'status': 'starting', 'errors': []}
    manifest = output / 'session.json'
    manifest.write_text(json.dumps(session, indent=2), encoding='utf-8')
    print(f'Session: {output.resolve()}')
    print('Close ComPortMaster for BOTH ports. USB console baud is NOT inter-board baud.')
    if args.interactive:
        print('Interactive: l ?, r ?, both ?; one ASCII key, case preserved, no newline.')
        print('Submenus: l/r/both value 40 = decimal digits + CR; '
              'l/r/both enter = CR (keep value).')
        print('both m/value/enter require both boards at the same menu field; '
              'check prompts. Sequential, not atomic; use l/r to recover desync.')
        print('Bare q/EOF or Ctrl+C = monitor exit; l q/r q/both q = BOARD mute toggle.')
        print('Only entered commands are sent; no automatic startup/exit/retry commands.')
        print('E/X robot PWM, A async binocular control, R/P and camera keys u/f/j/h/i/k '
              'can affect motion; blocked unless '
              '--allow-motion-commands. Other keys can also change board state.')
        if args.allow_motion_commands:
            print('WARNING: motion-related commands enabled; robot/camera may move.')
        if args.filter == 'stereo':
            print('Host TX results always display; non-[ST] board replies are hidden '
                  'on screen. Use --filter all to view replies; full RX logs retained.')
        if args.filter == 'commands':
            print(f'Commands view: per-target {args.response_window:g}s reply windows; '
                  'periodic traces/ST hidden. Heuristic only: firmware has no request IDs.')
    else:
        print('Receive-only: no capture, reset, mute, or robot commands. Ctrl+C = exit.')
    print('Log UTC labels are HOST receipt times, NOT camera exposure timestamps.')
    logs = None
    monitor = None
    status = 0
    try:
        with ExitStack() as stack:
            logs = MonitorLogs(output, args.filter, display_format=args.format,
                               response_window=args.response_window)
            stack.callback(logs.close)
            ports = []
            for name in (args.left.strip(), args.right.strip()):
                port = open_port(serial, name, args.baud)
                stack.callback(port.close)
                ports.append(port)
            monitor = ConsoleMonitor(ports, logs, args.interactive, args.allow_motion_commands)
            stack.callback(monitor.close)
            monitor.start()
            session['status'] = 'running'
            manifest.write_text(json.dumps(session, indent=2), encoding='utf-8')
            display_logs(monitor)
    except KeyboardInterrupt:
        session['status'] = 'interrupted'
        status = 130
    except (OSError, ValueError) as exc:
        session['errors'].append(f'{type(exc).__name__}: {exc}')
        status = 1
    finally:
        if monitor is not None:
            while not monitor.errors.empty():
                session['errors'].append(monitor.errors.get_nowait())
        if session['errors']:
            session['status'] = 'error'
            status = 1
        elif session['status'] != 'interrupted':
            session['status'] = 'stopped'
        if logs is not None:
            session['stats'] = logs.stats
            session['commands'] = logs.commands
        session['ended_utc'] = datetime.now(timezone.utc).isoformat()
        manifest.write_text(json.dumps(session, indent=2), encoding='utf-8')
    for error in session['errors']:
        print(f'ERROR: {error}')
    if logs is not None:
        for side, stats in logs.stats.items():
            print(f'[{LABELS[side]}] bytes={stats["bytes"]} lines={stats["lines"]} '
                  f'display_dropped={stats["display_dropped"]}')
    print(f'Stopped. Logs: {output.resolve()}')
    return status


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print('\nStopped; no automatic exit command was sent to the boards.')
        raise SystemExit(130)
    except (OSError, ValueError) as exc:
        print(f'ERROR: {exc}')
        raise SystemExit(1)
