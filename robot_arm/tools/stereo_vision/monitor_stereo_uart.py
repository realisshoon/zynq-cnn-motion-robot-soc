"""Log both board consoles with OBS video; optionally send keys, values and filters."""

import argparse
from contextlib import ExitStack
from datetime import datetime, timezone
from decimal import Decimal
import json
import math
import os
from pathlib import Path
import queue
import re
import sys
import subprocess
import threading
import time
import webbrowser


SIDES = ('left', 'right')
LABELS = {'left': 'L', 'right': 'R'}
MAX_LINE_BYTES = 65536
RECORD_SELECT_TIMEOUT = 5.0
RECORD_UI_STATES = ('IDLE', 'STOP_RECORD', 'STOP_MENU', 'NAME', 'MENU', 'SAVE',
                    'START_RECORD')
RECORD_STATE_PATTERN = re.compile(
    r'\[REC_STATE\] schema=1 side=([lr]) ui=('
    + '|'.join(RECORD_UI_STATES)
    + r') mode=(LIVE|RECORDING|ALIGNING|PLAYING|HOLDING) pwm=([01]) '
    r'ram_samples=([0-9]+) entries=([0-9]+) selected=([0-9]+) '
    r'playing=([0-9]+) delete=([0-9]+) unsaved=([01])')
COMMAND_TARGETS = {'l': ('left',), 'r': ('right',), 'both': SIDES}
MOTION_COMMANDS = frozenset('RPEXAufjhikOH')
MENU_PROMPT_END = b'Enter=keep]: '
VERBOSE_TRACE_TAGS = frozenset(('A1', 'P3', 'A2', 'TK', 'SM', 'CN', 'CAM', 'IN', 'CS',
                                'RAW', 'PAIR', 'PIX', 'PG', 'RQ', 'MS', 'GM', 'GO'))
FILTER_PARAMETERS = {
    ('2d', 'ema', 'tau'): ('EMA', Decimal('0.001'), Decimal('1'), 1000000),
    ('3d', 'min'): ('MIN', Decimal('0.01'), Decimal('10'), 1000),
    ('3d', 'beta'): ('BETA', Decimal('0'), Decimal('0.1'), 1000000),
    ('3d', 'derivative'): ('DERIVATIVE', Decimal('0.01'), Decimal('10'), 1000),
}


def parse_filter_command(parts):
    if parts[0] != 'r':
        raise ValueError('Filter commands require RIGHT target r; l/both are not supported')
    if len(parts) == 3 and parts[2] in ('show', 'default'):
        return ('r', f'~F,{parts[2].upper()}\r')
    parameter = FILTER_PARAMETERS.get(tuple(parts[2:-1]))
    if parameter is None:
        raise ValueError('Expected r filter show/default, r filter 2d ema tau <seconds>, '
                         'or r filter 3d min/beta/derivative <decimal>')
    token = parts[-1]
    if re.fullmatch(r'[0-9]+(?:\.[0-9]+)?', token) is None:
        raise ValueError('Filter values require a plain finite ASCII decimal; '
                         'no signs or exponents')
    operation, minimum, maximum, scale = parameter
    value = Decimal(token)
    if not minimum <= value <= maximum:
        raise ValueError(f'Filter {operation} must be within {minimum}..{maximum}')
    numerator, denominator = value.as_integer_ratio()
    scaled, remainder = divmod(numerator * scale, denominator)
    if remainder:
        raise ValueError(f'Filter {operation} must exactly represent an integer at '
                         f'scale {scale}; no rounding')
    return ('r', f'~F,{operation},{scaled}\r')


def parse_command(text):
    text = text.removesuffix('\n').removesuffix('\r')
    if any(character != '\t' and not 32 <= ord(character) <= 126
           for character in text):
        raise ValueError('Use ASCII only: l/r/both <key>, l/r/both value <digits>, '
                         'l/r/both enter, r filter <setting>, or q')
    parts = text.split()
    if not parts:
        return None
    if parts == ['q']:
        return ('exit', None)
    if len(parts) == 3 and parts[0] in COMMAND_TARGETS and parts[1] == 'settings':
        if parts[2] not in ('show', 'save', 'load'):
            raise ValueError('Expected l/r/both settings show/save/load')
        return (parts[0], f'@CFG,{parts[2].upper()}\r')
    if len(parts) == 1 and re.fullmatch(r'[0-9]{1,2}', parts[0]):
        if not 1 <= int(parts[0]) <= 32:
            raise ValueError('Choose a listed record number within 1..32')
        return ('r', f'!REC,SELECT,{int(parts[0])}\r')
    if len(parts) >= 2 and parts[1] == 'record':
        if parts[0] not in ('l', 'r'):
            raise ValueError('Record library commands require one target l or r')
        if len(parts) == 3 and parts[2] in ('list', 'cancel'):
            return (parts[0], f'!REC,{parts[2].upper()}\r')
        if (len(parts) == 4 and parts[2] == 'name' and
                re.fullmatch(r'[A-Za-z0-9_-]{1,24}', parts[3])):
            return (parts[0], f'!REC,NAME,{parts[3]}\r')
        if (len(parts) == 4 and parts[2] in ('select', 'delete', 'confirm') and
                re.fullmatch(r'[0-9]{1,2}', parts[3]) and 1 <= int(parts[3]) <= 32):
            return (parts[0], f'!REC,{parts[2].upper()},{int(parts[3])}\r')
        raise ValueError('Expected l/r record list/cancel, l/r record name <1..24 letters/digits/_/->, '
                         'or l/r record select/delete/confirm <1..32>')
    if len(parts) >= 2 and parts[1] == 'filter':
        return parse_filter_command(parts)
    if len(parts) == 2 and parts[0] in COMMAND_TARGETS and parts[1] == 'enter':
        return (parts[0], '\r')
    if (len(parts) == 3 and parts[0] in COMMAND_TARGETS and parts[1] == 'value'
            and 1 <= len(parts[2]) <= 6 and parts[2].isascii() and parts[2].isdigit()):
        return (parts[0], parts[2] + '\r')
    if (len(parts) != 2 or parts[0] not in COMMAND_TARGETS or
            len(parts[1]) != 1 or not 33 <= ord(parts[1]) <= 126):
        raise ValueError('Expected l/r/both <key>, l/r/both value <1..6 decimal digits>, '
                         'l/r/both enter, or r filter <setting>')
    return tuple(parts)


class RecordState:
    def __init__(self):
        self.menu = False
        self.entries = {}
        self.delete_confirm = None
        self.name_prompt = False
        self.playing = None
        self.last_deleted = None
        self.select_pending = None
        self.selection = None
        self.delete_requested = False
        self.schema = None
        self.ui_status_received = False
        self.ui = None
        self.mode = None
        self.pwm = None
        self.ram_samples = None
        self.entries_count = None
        self.selected_id = None
        self.unsaved = None

    def restore_menu(self):
        self.menu = (self.ui == 'MENU' or
                     (self.ui == 'IDLE' and self.delete_confirm is not None)
                     if self.ui_status_received else
                     bool(self.entries) and not self.name_prompt)

    def reconcile_ui(self, ui):
        if self.ui != ui or ui != 'MENU':
            self.selection = None
            self.delete_confirm = None
            self.delete_requested = False
        self.ui = ui
        self.name_prompt = ui in ('NAME', 'SAVE')
        self.menu = ui == 'MENU'
        self.select_pending = None

    def reconcile_status(self, line, side):
        status = RECORD_STATE_PATTERN.fullmatch(line)
        if status:
            if status[1] != side[0]:
                return
            previous_delete = self.delete_confirm
            self.schema = 1
            self.ui_status_received = True
            self.reconcile_ui(status[2])
            self.mode = status[3]
            self.pwm = status[4] == '1'
            self.ram_samples = int(status[5])
            self.entries_count = int(status[6])
            self.selected_id = int(status[7])
            playing_id = int(status[8])
            playing_name = (self.playing[1] if self.playing and
                            self.playing[0] == playing_id else '')
            self.playing = ((playing_id, self.entries.get(playing_id, playing_name))
                            if playing_id else None)
            self.delete_confirm = int(status[9]) or None
            self.delete_requested = self.delete_confirm is not None
            if self.delete_confirm is not None:
                self.selection = self.delete_confirm
                self.menu = self.ui in ('IDLE', 'MENU')
            elif previous_delete is not None:
                self.selection = None
            self.unsaved = status[10] == '1'
            if not self.entries_count:
                self.entries = {}
            return
        legacy = re.fullmatch(
            r'\[REC\] entries=([0-9]+) selected=([0-9]+) ui=([0-6])(?:;.*)?', line)
        if legacy and self.schema is None:
            self.ui_status_received = True
            self.entries_count = int(legacy[1])
            self.selected_id = int(legacy[2])
            self.reconcile_ui(RECORD_UI_STATES[int(legacy[3])])
            if self.name_prompt:
                self.unsaved = True
            if self.ui != 'IDLE':
                self.playing = None
            if not self.entries_count:
                self.entries = {}


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
        self.records = {side: RecordState() for side in SIDES}
        self.uart_stop_supported = {side: False for side in SIDES}
        self.settings_supported = {side: False for side in SIDES}
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
            if line.lstrip().startswith(('[REC]', '[REC_STATE]', '[CFG]', '[UART]')):
                return True
            if line.lstrip().startswith('#'):
                return False
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
        target = 'l' if side == 'left' else 'r'
        record = self.records[side]
        if line.startswith('[REC_STATE]') or re.match(r'\[REC\] entries=', line):
            status = RECORD_STATE_PATTERN.fullmatch(line)
            legacy = re.fullmatch(
                r'\[REC\] entries=([0-9]+) selected=([0-9]+) ui=([0-6])(?:;.*)?', line)
            if ((status and status[1] == side[0]) or
                    (legacy and record.schema is None)):
                details = f'녹화 UI={record.ui}'
                if record.mode is not None:
                    details += f' / 동작={record.mode} / PWM={int(record.pwm)}'
                if record.ram_samples is not None:
                    details += f' / RAM {record.ram_samples}개'
                if record.unsaved:
                    details += ' / SD 미저장'
                details += f' / SD 레코드 {record.entries_count}개 / 선택={record.selected_id}'
                if record.name_prompt and record.ui == 'NAME':
                    details += f' / 저장할 이름 입력 후 Enter / 취소: {target} record cancel'
                elif record.ui == 'SAVE':
                    details += ' / SD 저장 응답 대기'
                elif record.menu:
                    details += f' / 목록: {target} record list'
                if record.playing:
                    details += f' / 재생={record.playing[0]} / 중지: {target} P'
                if record.delete_confirm is not None:
                    details += f' / 삭제 확인={record.delete_confirm} / Enter: 삭제 / Esc: 취소'
                line = details
        if line.startswith('[REC]'):
            if line.startswith('[REC] SD records; select:'):
                line = '=== 재생할 레코드 선택 ==='
            else:
                entry = re.fullmatch(r'\[REC\] ([0-9]+) ([A-Za-z0-9_-]+) samples=[0-9]+ duration_ms=[0-9]+(?: selected)?', line)
                if entry:
                    line = f'{entry[1]}. {entry[2]}'
                elif line.startswith('[REC] count='):
                    count = re.match(r'\[REC\] count=([0-9]+)', line)
                    line = ('번호 선택 → 같은 번호: 재생 / Backspace → Enter: 삭제 / Esc: 취소'
                            if count and int(count[1]) else '저장된 레코드가 없습니다.')
                elif line.startswith('[REC] enter name:'):
                    line = f'녹화 종료·정착 완료. 저장할 이름 입력 후 Enter (영문/숫자/_/-, 1~24자). 취소: {target} record cancel'
                elif line.startswith('[REC] SAVE_FAILED'):
                    line = 'SD 저장 실패 — RAM 기록 유지. 이름을 다시 입력해 재시도하세요.'
                elif line.startswith('[REC] name rejected'):
                    line = '이름 저장 요청 거절.'
                    if record.unsaved or record.name_prompt:
                        line += ' RAM 기록 SD 미저장 상태 유지.'
                    line += f' 보드 NAME 상태에서 이름 재입력 / 상태 확인: {target} V'
                else:
                    played = re.match(r'\[REC\] PLAY id=([0-9]+) name=([A-Za-z0-9_-]+)', line)
                    saved = re.match(r'\[REC\] SAVED id=([0-9]+) name=([A-Za-z0-9_-]+)', line)
                    deleted = re.match(r'\[REC\] DELETED id=([0-9]+)', line)
                    if played:
                        line = f'재생 시작: {played[1]}. {played[2]} — 시작 자세 이동 후 무한 반복 / 중지: {target} P'
                    elif saved:
                        line = f'SD 저장 완료: {saved[1]}. {saved[2]} / 목록: {target} P'
                    elif deleted:
                        name = (record.last_deleted[1] if record.last_deleted and
                                record.last_deleted[0] == int(deleted[1]) else '')
                        line = f'삭제 완료: {deleted[1]}. {name} / 남은 목록: {target} P'
                    elif line.startswith('[REC] playback stop=1'):
                        line = f'재생 중지 완료 — LIVE 복귀, 추종 자동 재개 없음 / 목록: {target} P'
                    elif '[REC] confirm deleting ' in line:
                        confirmation = re.match(r'\[REC\] confirm deleting ([0-9]+) ([A-Za-z0-9_-]+)', line)
                        if confirmation:
                            line = f'삭제 확인: {confirmation[1]}. {confirmation[2]} / Enter: 삭제 / Esc: 취소'
        try:
            prefix = f'{timestamp} ' if self.display_format == 'tagged' else ''
            self.display.put_nowait(f'{prefix}[{LABELS[side]}] {line}')
        except queue.Full:
            self.stats[side]['display_dropped'] += 1

    def emit_locked(self, side, raw_line, timestamp, suffix='', display_start=0):
        line = raw_line.decode('utf-8', errors='replace') + suffix
        if line.strip().startswith('Zybo Z7-20 CNN camera platform,'):
            self.settings_supported[side] = False
            self.uart_stop_supported[side] = False
            self.records[side] = RecordState()
        record = self.records[side]
        record.reconcile_status(line, side)
        capability = re.match(r'\[CFG\] supported=1 role=([12]) protocol=@CFG schema=1;', line)
        if capability and int(capability[1]) == (1 if side == 'left' else 2):
            self.settings_supported[side] = True
        stop_capability = re.fullmatch(
            r'\[UART\] stop_escape=1 protocol=ESC_X_S frame_timeout_ms=250 role=([12])', line)
        if stop_capability and int(stop_capability[1]) == (1 if side == 'left' else 2):
            self.uart_stop_supported[side] = True
        if line.startswith('[REC]'):
            if line.startswith(('[REC] enter name:', '[REC] SAVE_FAILED')):
                record.reconcile_ui('NAME')
                record.unsaved = True
            if line.startswith('[REC] SAVED'):
                record.reconcile_ui('IDLE')
                record.unsaved = False
            if line.startswith('[REC] cancelled'):
                record.reconcile_ui('IDLE')
            if line.startswith('[REC] deletion confirmation cleared'):
                record.delete_confirm = None
                record.selection = None
                record.delete_requested = False
            played = re.match(r'\[REC\] PLAY id=([0-9]+) name=([A-Za-z0-9_-]+)', line)
            if played:
                record.playing = (int(played[1]), played[2])
                record.reconcile_ui('IDLE')
            if line.startswith(('[REC] select rejected', '[REC] play rejected', '[REC] rejected')):
                if record.select_pending is not None:
                    record.select_pending = None
                    record.restore_menu()
                if line.startswith('[REC] rejected UI_BUSY'):
                    record.menu = False
                    record.selection = None
                    record.delete_confirm = None
                    record.delete_requested = False
            if line.startswith('[REC] playback stop=1'):
                record.playing = None
            if line.startswith('[REC] SD records; select:'):
                record.menu = False
                record.entries = {}
                if record.schema is None or record.delete_confirm is None:
                    record.delete_confirm = None
                    record.selection = None
                    record.delete_requested = False
            entry = re.fullmatch(r'\[REC\] ([0-9]+) ([A-Za-z0-9_-]+) samples=[0-9]+ duration_ms=[0-9]+(?: selected)?', line)
            if entry:
                record.entries[int(entry[1])] = entry[2]
                if record.playing and record.playing[0] == int(entry[1]):
                    record.playing = (int(entry[1]), entry[2])
            if line.startswith('[REC] count='):
                record.restore_menu()
            removed = re.match(r'\[REC\] (?:DELETED|DELETE_PARTIAL_REQUIRES_SD_REVIEW) id=([0-9]+)', line)
            if removed:
                deleted_id = int(removed[1])
                record.last_deleted = (deleted_id, record.entries.pop(deleted_id, ''))
                record.delete_confirm = None
                record.selection = None
                record.delete_requested = False
                record.restore_menu()
            if line.startswith('[REC]') and 'confirm' in line.lower():
                target = 'l' if side == 'left' else 'r'
                confirmation = re.search(rf'{target} record confirm ([0-9]+)', line)
                if confirmation:
                    record.delete_confirm = int(confirmation[1])
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
        self.record_side = None

    def record_input_side(self, field):
        with self.logs.lock:
            if self.record_side is not None:
                return self.record_side if getattr(self.logs.records[self.record_side], field) else None
            active = [side for side in SIDES if getattr(self.logs.records[side], field)]
        return active[0] if len(active) == 1 else None

    def handle_menu_key(self, key, side=None):
        self.poll_record_selection()
        side = side or self.record_input_side('menu')
        if side is None:
            return False
        target = 'l' if side == 'left' else 'r'
        with self.logs.lock:
            record = self.logs.records[side]
            active = record.menu
            entries = dict(record.entries)
            confirmation = record.delete_confirm
            pending = record.select_pending is not None
        if not active:
            record.selection = None
            record.delete_requested = False
            return False
        self.record_side = side
        if pending and key in '123456789\x08\x7f\r\n':
            print(f'[MENU {LABELS[side]}] 보드의 재생 승인 응답을 기다리는 중입니다. 자동 재전송하지 않습니다.', flush=True)
            return True
        if key in ('\x08', '\x7f') and record.selection in entries:
            record.delete_requested = True
            with self.logs.lock:
                record.delete_confirm = None
            self.handle_input(f'{target} record delete {record.selection}')
            print(f'[MENU {LABELS[side]}] 삭제 확인 응답을 기다린 뒤 Enter로 삭제. Esc는 취소.', flush=True)
            return True
        if key in ('\r', '\n') and record.delete_requested:
            if confirmation == record.selection:
                with self.logs.lock:
                    selected = record.selection
                    record.delete_requested = False
                    record.selection = None
                    record.delete_confirm = None
                self.handle_input(f'{target} record confirm {selected}')
            else:
                print(f'[MENU {LABELS[side]}] 보드의 삭제 확인 응답을 기다려 주세요.', flush=True)
            return True
        if key == '\x1b':
            self.handle_input(f'{target} record cancel')
            return True
        if key in '123456789' and len(key) == 1 and int(key) in entries:
            selected = int(key)
            if record.selection == selected and not record.delete_requested:
                with self.logs.lock:
                    record.selection = None
                if not self.handle_input(f'{target} record select {selected}'):
                    with self.logs.lock:
                        if record.menu and record.selection is None and record.delete_confirm is None:
                            record.selection = selected
            else:
                record.selection = selected
                record.delete_requested = False
                with self.logs.lock:
                    record.delete_confirm = None
                print(f'[MENU {LABELS[side]}] {selected}. {entries[selected]} 선택 — 같은 번호: 재생 / Backspace: 삭제 / Esc: 취소', flush=True)
            return True
        return False

    def poll_record_selection(self):
        for side in SIDES:
            with self.logs.lock:
                record = self.logs.records[side]
                pending = record.select_pending
                if pending is None or time.monotonic() < pending['until']:
                    continue
                record.select_pending = None
                record.restore_menu()
            target = 'l' if side == 'left' else 'r'
            self.logs.record_command({'input': f'{target} record select {pending["id"]}',
                'target': target, 'command': f'!REC,SELECT,{pending["id"]}\r',
                'status': 'response_timeout', 'results': [],
                'error': f'재생 승인 응답이 5초 안에 오지 않았습니다. 목록 유지·자동 재전송 없음. {target} V/{target} P로 보드 상태를 확인하세요.'})

    def handle_input(self, text):
        if not self.interactive or self.stop.is_set():
            return
        event = {'input': text.rstrip('\r\n'), 'target': None, 'command': None,
                 'results': []}
        with self.logs.lock:
            naming = any(record.name_prompt for record in self.logs.records.values())
            menus = any(record.menu for record in self.logs.records.values())
        entered = text.strip()
        if naming and entered and entered.split()[0] not in ('r', 'l', 'both', 'q'):
            side = self.record_input_side('name_prompt')
            if side is None:
                self.logs.record_command({**event, 'status': 'rejected', 'error':
                    '이름 입력 대상이 불명확합니다. l/r record name <name>으로 보드를 지정하세요.'})
                return False
            text = f'{"l" if side == "left" else "r"} record name {entered}'
        elif re.fullmatch(r'[0-9]{1,2}', entered) and (menus or self.record_side is not None):
            side = self.record_side or self.record_input_side('menu')
            if side is None:
                self.logs.record_command({**event, 'status': 'rejected', 'error':
                    '번호 입력 대상이 불명확합니다. l/r record select <number>으로 보드를 지정하세요.'})
                return False
            text = f'{"l" if side == "left" else "r"} record select {entered}'
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
        if command.startswith('@CFG,'):
            with self.logs.lock:
                unsupported = [side for side in COMMAND_TARGETS[target] if not self.logs.settings_supported[side]]
            if unsupported:
                self.logs.record_command({**event, 'status': 'blocked', 'error':
                    '설정 기능 지원 미확인: 새 BOOT 설치 후 l/r/both t로 확인하세요. '
                    '구형 BOOT에는 @CFG를 전송하지 않습니다.'})
                return
        motion_command = command in MOTION_COMMANDS or command.startswith('!REC,SELECT,')
        if motion_command and not self.allow_motion_commands:
            self.logs.record_command({**event, 'status': 'blocked', 'error':
                'Robot PWM/recording/replay or camera PWM/configuration key; motion risk. '
                'Requires --interactive --allow-motion-commands'})
            return
        if motion_command:
            print(f'[HOST] WARNING: {command!r} can change robot PWM/live control/recording/replay '
                   'or camera PWM/configuration; motion may occur.', flush=True)
        selecting = command.startswith('!REC,SELECT,')
        record_side = COMMAND_TARGETS[target][0] if target in ('l', 'r') else None
        if selecting:
            self.poll_record_selection()
            with self.logs.lock:
                record = self.logs.records[record_side]
                busy = record.select_pending is not None
                if not busy:
                    record.select_pending = {
                        'id': int(command.split(',')[2]),
                        'until': time.monotonic() + RECORD_SELECT_TIMEOUT}
                    record.delete_confirm = None
            if busy:
                self.logs.record_command({**event, 'status': 'blocked', 'error':
                    '재생 승인 대기 중입니다. 응답 또는 시간 초과까지 재전송하지 않습니다.'})
                return False
        elif command.startswith(('!REC,DELETE,', '!REC,CONFIRM,')):
            with self.logs.lock:
                busy = self.logs.records[record_side].select_pending is not None
            if busy:
                self.logs.record_command({**event, 'status': 'blocked', 'error':
                    '재생 승인 대기 중에는 삭제하지 않습니다.'})
                return False
        if command in ('P', 'R', 'X', 'S') or command.startswith('!REC,CANCEL'):
            with self.logs.lock:
                for side in COMMAND_TARGETS[target]:
                    record = self.logs.records[side]
                    record.menu = False
                    record.delete_confirm = None
                    record.select_pending = None
                    record.selection = None
                    record.delete_requested = False
        if record_side is not None and (command in ('P', 'R') or command.startswith('!REC,')):
            self.record_side = record_side
        if target == 'both' and (command == 'm' or
                                 (command.endswith('\r') and not command.startswith('@CFG,'))):
            print('[HOST] WARNING: BOTH submenu commands require BOTH boards at the same '
                  'menu field (or both ready to enter m). Check [L]/[R] prompts; '
                  'sequential, not atomic; no automatic retry.', flush=True)
        interrupted = False
        failed = False
        payload = (('\x1b' + command) if command in ('X', 'S') else command).encode('ascii')
        if command in ('X', 'S'):
            with self.logs.lock:
                unverified = [LABELS[side] for side in COMMAND_TARGETS[target]
                              if not self.logs.uart_stop_supported[side]]
            if unverified:
                print('[HOST] 수신 취소 지원 미확인: ' + ','.join(unverified) +
                      '. 구형 BOOT에서는 메뉴/잘린 명령 탈출을 보장하지 않습니다. 새 BOOT 적용 후 l/r/both t로 확인하세요.', flush=True)
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
        if selecting and event['status'] != 'sent':
            with self.logs.lock:
                record = self.logs.records[record_side]
                record.select_pending = None
                record.restore_menu()
        self.logs.record_command(event)
        if selecting and event['status'] == 'sent':
            print('[MENU] 재생 요청 전송 완료 — 보드 PLAY 승인 후 메뉴를 닫습니다.', flush=True)
        if target in ('l', 'r') and command == 'P' and event['status'] == 'sent':
            print('[HOST] 메뉴에서 번호 선택 → 같은 번호: 재생 / Backspace → Enter: 삭제. '
                  f'재생 중 {target} P: 중지.', flush=True)
        if command.startswith('@CFG,') and event['status'] == 'sent':
            print('[HOST] 설정 save/load: 해당 보드 PWM OFF(r/l X), CNN OFF가 필요합니다. '
                  'r/l t로 확인하고 ON이면 r/l a로 끈 뒤 실행하세요. '
                  '[CFG] SAVED/LOADED 응답으로 완료를 확인하세요.', flush=True)
        if interrupted:
            raise KeyboardInterrupt
        return event['status'] == 'sent'

    def process_input(self):
        if not self.interactive or self.stop.is_set():
            return
        self.poll_record_selection()
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
            if isinstance(text, tuple) and text[0] == 'key':
                self.handle_menu_key(text[1], text[2])
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
                target=read_console_input, args=(sys.stdin, self.input_queue, self.stop, self),
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


def read_console_input(stream, commands, stop, monitor=None):
    if (os.name == 'nt' and stream is sys.stdin and
            getattr(stream, 'isatty', lambda: False)() and monitor is not None):
        read_windows_console(commands, stop, monitor)
        return
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


def read_windows_console(commands, stop, monitor):
    import msvcrt

    buffer = ''
    while not stop.is_set():
        if not msvcrt.kbhit():
            stop.wait(.02)
            continue
        key = msvcrt.getwch()
        if key in ('\x00', '\xe0'):
            msvcrt.getwch()
            continue
        if key == '\x03':
            commands.put('q\n')
            return
        side = monitor.record_input_side('menu')
        with monitor.logs.lock:
            listed = dict(monitor.logs.records[side].entries) if side else {}
        if not buffer and side and (key in ('\x08', '\x7f', '\x1b', '\r') or
                                   (key in '123456789' and int(key) in listed)):
            commands.put(('key', key, side))
        elif key == '\r':
            print('', flush=True)
            commands.put(buffer + '\n')
            buffer = ''
        elif key == '\x08':
            if buffer:
                buffer = buffer[:-1]
                print('\b \b', end='', flush=True)
        elif key >= ' ':
            buffer += key
            print(key, end='', flush=True)


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


def create_obs_recorder(output):
    from obs_session_recorder import SessionRecorder

    return SessionRecorder(output)


def create_xyz_review(output, open_browser=True):
    output = Path(output).resolve()
    viewer = Path(__file__).with_name('stereo_xyz_viewer.py')
    result = subprocess.run(
        [sys.executable, '-B', str(viewer), '--session', str(output),
         '--no-geometry', '--allow-empty'], capture_output=True,
        encoding='utf-8', errors='replace')
    (output / 'xyz_review_build.log').write_text(
        result.stdout + result.stderr, encoding='utf-8')
    if result.returncode:
        raise RuntimeError(f'XYZ viewer failed ({result.returncode}); '
                           f'see {output / "xyz_review_build.log"}')
    review = output / 'xyz_review/review.html'
    if not review.is_file():
        return {'status': 'skipped', 'reason': 'No PAIR rows received'}
    summary = json.loads((review.parent / 'summary.json').read_text(encoding='utf-8'))
    opened = False
    if open_browser:
        try:
            opened = bool(webbrowser.open(review.as_uri()))
        except Exception as exc:
            print(f'[XYZ] WARNING: browser unavailable: {exc}', flush=True)
    return {'status': 'complete', 'path': str(review), 'browser_opened': opened,
            'pairs': summary['pairs'], 'angle_pairs': summary['angle_pairs'],
            'geometry': 'recorded_P3_and_A1_A2_only'}


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
    parser.add_argument('--no-video', action='store_true',
                        help='Disable automatic OBS recording; UART logs are still saved')
    parser.add_argument('--no-review', action='store_true',
                        help='Disable automatic combined video/XYZ/angle review after exit')
    parser.add_argument('--no-open-review', action='store_true',
                        help='Generate the review without opening a browser after exit')
    parser.add_argument('--interactive', action='store_true',
                        help='Enable l/r/both <key>, l/r/both value <digits>, '
                             'l/r/both enter, r filter <setting>; bare q exits')
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
               'obs': {'status': 'disabled' if args.no_video else 'starting'},
               'xyz_review': {'status': 'disabled' if args.no_review else 'pending'},
               'status': 'starting', 'errors': []}
    manifest = output / 'session.json'
    manifest.write_text(json.dumps(session, indent=2), encoding='utf-8')
    print(f'Session: {output.resolve()}')
    print('Close ComPortMaster for BOTH ports. USB console baud is NOT inter-board baud.')
    if args.interactive:
        print('Interactive: l ?, r ?, both ?; one ASCII key, case preserved, no newline.')
        print('Submenus: l/r/both value 40 = decimal digits + CR; '
              'l/r/both enter = CR (keep value).')
        print('RIGHT filters: r filter show/default; r filter 2d ema tau <seconds>; '
              'r filter 3d min/beta/derivative <decimal>. New filter BOOT required; '
              'runtime RAM settings, no SD persistence.')
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
    if not args.no_review:
        print('[XYZ] After q/Ctrl+C: combined video, recorded 3D and A1/A2 review; '
              'fixed layout. No automatic board commands.')
    logs = None
    monitor = None
    video = None
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
            if args.no_video:
                print('[OBS] video disabled by --no-video; UART logging only.', flush=True)
            else:
                try:
                    video = create_obs_recorder(output)
                    video.start()
                    session['obs'] = video.summary()
                except Exception as exc:
                    if video is not None:
                        video.close()
                        video = None
                    session['obs'] = {'status': 'error', 'error': str(exc)}
                    print(f'[OBS] WARNING: video unavailable: {exc}; '
                          'UART logging continues WITHOUT video.', flush=True)
                manifest.write_text(json.dumps(session, indent=2), encoding='utf-8')
            display_logs(monitor)
    except KeyboardInterrupt:
        session['status'] = 'interrupted'
        status = 130
    except (OSError, ValueError) as exc:
        session['errors'].append(f'{type(exc).__name__}: {exc}')
        status = 1
    finally:
        if video is not None:
            video.close()
            session['obs'] = video.summary()
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
    if not args.no_review:
        try:
            session['xyz_review'] = create_xyz_review(output, not args.no_open_review)
            review = session['xyz_review']
            if review['status'] == 'complete':
                print(f'[XYZ] Combined review: {review["path"]}', flush=True)
                if not review['browser_opened']:
                    print('[XYZ] Open review.html to view video + XYZ + angles together.')
            else:
                print(f'[XYZ] Review skipped: {review["reason"]}', flush=True)
        except Exception as exc:
            session['xyz_review'] = {'status': 'error', 'error': str(exc)}
            print(f'[XYZ] WARNING: {exc}; UART logs and OBS video remain saved.', flush=True)
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
