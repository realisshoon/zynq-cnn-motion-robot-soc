"""CNN-v4 integer contract. Numeric/transaction reference, not an AXI cycle simulator.
Uses only NumPy; never imports torch or downloads a checkpoint.

=====================================================================
[하드웨어 모듈 대응 지도 — 이 파일이 검증하는 것 / 검증하지 못하는 것]

이 파일은 "각 연산의 결과 숫자가 맞는지"만 검증한다. RTL의 valid/ready
핸드셰이크, stall, FSM 상태 전이 같은 "타이밍/제어"는 전혀 흉내내지
않는다(파일 맨 위 docstring에도 "not an AXI cycle simulator"라고 명시됨).

■ 연산(숫자)이 정확히 대응되는 모듈 — 이 파일로 검증 가능
    downsample()      -> downsample_module
    color_markers()    -> color_marker_detect
    conv_acc(conv0)    -> input_conv_pe
    conv_acc(dw)       -> depthwise_conv_pe
    conv_acc(pw/head)  -> pointwise_conv_pe (heatmap/offset 헤드도 재사용)
    requant()          -> 위 세 PE 내부의 재양자화+saturate 스테이지
    rshift_even()      -> 모든 PE가 공유하는 RNE 반올림 공식
    decode() 앞부분     -> argmax_threshold
    decode() 뒷부분     -> coord_restore
    default_manifest() -> layer_param_rom이 서빙하는 cfg_desc256 내용의 원본
    pack_layer()       -> weight_bram_swap_fsm이 최종적으로 저장/서빙하는
                          바이트 배치 규칙 (PC에서 미리 이 배치로 재배열)

■ 이 파일에 "숫자 계산"만 있고, RTL의 "제어/배선" 자체는 안 담긴 모듈
  (즉 이 파일로는 검증이 안 되고, 별도 protocol testbench가 필요함)
    line_buffer         -> forward() 안에서 xp[...]로 윈도우를 한번에
                           잘라내는 numpy 슬라이싱이 이 역할을 "숨겨서"
                           대신하고 있음. 실제 RTL처럼 tap을 시간순으로
                           스트리밍하거나 stride=2 게이팅을 언제 하는지는
                           이 파일에 없음.
    feature_map_io       -> forward()가 body를 그냥 파이썬 변수로 들고
                           있다가 다음 op에 바로 씀. 실제로는 매 stage마다
                           DDR(FM_A/FM_B)에 썼다가 다시 읽어와야 하는데,
                           그 DMA 왕복이 이 파일엔 전혀 없음.
    weight_bram_swap_fsm -> PackedWeights가 주소 산수(word_addr 등)는
                           재현하지만, req_valid/req_ready 요청-응답
                           프로토콜 자체는 없음. 그냥 파이썬 배열
                           인덱싱으로 한번에 읽음.
    top_level_fsm         -> forward()의 for 루프가 "29개 연산을 순서대로
                           돌린다"는 큰 그림만 대응됨. cfg_valid 발행,
                           busy/done 대기, weight reload 금지 구간 같은
                           실제 FSM 상태 전이는 없음.
    cnn_accelerator_top   -> 이 파일엔 아예 대응 코드가 없음. 12개
                           모듈을 실제로 어떻게 배선하는지는 RTL에만
                           존재하는 정보.

즉 이 파일이 담보하는 건 "모든 모듈이 이상적으로 연결됐을 때, 결과
숫자가 정확히 이거여야 한다"는 기준선이고, "그 연결 자체가 제대로
동작하는가(핸드셰이크, stall, 타이밍)"는 각 모듈 페이지의 테스트
시나리오와 실제 RTL 시뮬레이션으로 따로 검증해야 한다.
=====================================================================
"""
from pathlib import Path
import argparse, hashlib, json, zipfile
import numpy as np

VERSION = 'CNN-v4.0'


def rshift_even(value, n):
    # [공용 설계 규칙 — RNE 구현식] q=p>>>N, r=p-(q<<N), h=1<<(N-1),
    # result=q+((r>h)||((r==h)&&(q&1))) 을 그대로 numpy로 옮긴 것.
    # 모든 PE(input_conv_pe/depthwise_conv_pe/pointwise_conv_pe)와
    # coord_restore가 내부에서 똑같은 이 수식을 RTL로 구현해야 함.
    a = np.asarray(value, dtype=np.int64)
    if not 0 <= n <= 31: raise ValueError('shift outside 0..31')
    if n == 0: return a.copy()
    q = a >> n
    r = a - (q << n)
    half = 1 << (n-1)
    return q + ((r > half) | ((r == half) & ((q & 1) != 0)))


def signed_bound(a, bits, label):
    # [하드웨어 대응 없음] 런타임 오버플로 체크가 아니라, pack/validate
    # 단계에서 "이 accumulator 폭이면 절대 넘치지 않는다"를 미리
    # 증명해두는 PC 전용 assert. RTL은 이 체크를 하지 않고,
    # 대신 accumulator 비트폭(signed24/25 등)을 애초에 넉넉히 설계함.
    a = np.asarray(a)
    if np.any(a < -(1 << (bits-1))) or np.any(a >= (1 << (bits-1))):
        raise OverflowError(label + ': signed width exceeded')


def requant(a, m, n=16, mode='body'):
    # [input_conv_pe / depthwise_conv_pe / pointwise_conv_pe 공통]
    # "누적값 x weight_M >> shift_N 후 saturate" 스테이지.
    # pointwise_conv_pe 페이지의 파이프라인 P8(곱셈)->P9(RNE)->P10(saturate)이
    # 바로 이 함수 한 줄 한 줄에 대응됨.
    signed_bound(a, 25, 'requant input')
    if np.any(m < 0) or np.any(m > 131071): raise OverflowError('M must fit unsigned 17 bits')
    p = np.asarray(a, dtype=np.int64) * np.asarray(m, dtype=np.int64)   # P8: 곱셈
    signed_bound(p, 43, '25x18 product')
    y = rshift_even(p, n)                                               # P9: RNE
    lo, hi = {'body': (0, 127), 'heatmap': (-128, 127), 'offset': (-32768, 32767)}[mode]
    return np.clip(y, lo, hi).astype(np.int16 if mode == 'offset' else np.int8)  # P10: saturate


class Weights:
    # [하드웨어 대응 없음, PC 전용] 원본 학습 결과(weights_export.zip)를
    # 읽는 클래스. 이 zip은 절대 하드웨어에 안 올라감 — pack의 입력일 뿐.
    def __init__(self, archive):
        self.archive = Path(archive)
        self.z = zipfile.ZipFile(self.archive)
        self.meta = json.loads(self.z.read('weights_export/scales.json'))
        self.sha256 = hashlib.sha256(self.archive.read_bytes()).hexdigest()

    def array(self, info, key, dtype):
        return np.frombuffer(self.z.read('weights_export/' + info[key]), dtype=dtype).copy()

    def layer(self, name):
        wi = self.meta[name + '.weight']; bi = self.meta[name + '.bias']
        if wi['shift_bits'] != 16 or bi['shift_bits'] != 16: raise ValueError('N must be 16')
        w = self.array(wi, 'bin_file', 'i1').reshape(wi['shape'])
        m = self.array(wi, 'm_bin_file', '<i4').astype(np.int64)
        b = self.array(bi, 'bin_file', 'i1').astype(np.int64)
        bm = self.array(bi, 'm_bin_file', '<i4').astype(np.int64)
        if len(m) != w.shape[0] or b.size != w.shape[0] or bm.size != b.size: raise ValueError('channel count')
        if not np.array_equal(m, wi['per_channel_M']) or not np.array_equal(bm, bi['per_channel_M']): raise ValueError('metadata mismatch')
        # [weight_bram_swap_fsm이 "가중치 로딩 시점에 사전계산"하기로 한 그 연산]
        # bias_accum = RNE(bias_int8 * bias_M / 65536). RTL의 각 PE는
        # 이미 계산된 이 값을 받아서 그냥 더하기만 하면 됨(곱셈 없음).
        ba = rshift_even(b * bm, 16)
        signed_bound(ba, 24, 'bias_accum')
        if np.any(m < 0) or np.any(m > 131071): raise OverflowError('unsupported M')
        return w.astype(np.int64), m, ba


class PackedWeights:
    """Read exactly the bytes loaded by the hardware, without the export ZIP."""
    # [weight_bram_swap_fsm의 주소 산수와 대응] req_addr로 지정한 위치의
    # 바이트를 읽어오는 로직을 파이썬으로 재현. 다만 이건 "한번에 배열
    # 인덱싱"이라 실제 req_valid/req_ready 요청-응답 사이클(1cycle 후
    # response)은 표현하지 않음 — 주소 계산식만 검증하는 용도.
    def __init__(self, path, manifest):
        self.blob = Path(path).read_bytes(); self.manifest = manifest
        if len(self.blob) != manifest['packed_bytes'] or hashlib.sha256(self.blob).hexdigest() != manifest['packed_sha256']:
            raise ValueError('packed payload size/hash mismatch')
        self.sha256 = manifest['weights_sha256']; self.layers = {}
        for o in manifest['ops']:
            raw = self.blob[o['weight_offset']:o['weight_offset'] + o['dma_bytes']]
            co, ci = o['cout'], o['cin']; g = (co + 3) // 4; t = (ci + 31) // 32
            q = np.frombuffer(raw[:o['param_offset']], dtype='i1')
            if o['kind'] == 'pw':
                # [pointwise_conv_pe가 pw_req_addr/pw_req_group으로 읽어오는
                #  그 word_addr = out_group*Gin + in_batch 공식의 역변환]
                w = q[:g * t * 128].reshape(g, t, 4, 32).transpose(0, 2, 1, 3).reshape(g * 4, t * 32)[:co, :ci, None, None]
            elif o['kind'] == 'dw':
                # [depthwise_conv_pe가 dw_w_req_addr로 읽어오는 배치 구조]
                cb = (co + 31) // 32
                w = q[:cb * 9 * 32].reshape(cb, 9, 32).transpose(0, 2, 1).reshape(cb * 32, 1, 3, 3)[:co]
            else:
                # [input_conv_pe가 conv0_req_addr로 통째로 읽어오는 24채널 블록]
                w = q[:co * 32].reshape(co, 32)[:, :27].reshape(co, 3, 3, 3).transpose(0, 3, 1, 2)
            pars = np.frombuffer(raw, dtype='<i4', count=g * 4 * 2, offset=o['param_offset']).reshape(g * 4, 2)[:co]
            self.layers[o['name']] = (w.astype(np.int64), pars[:, 1].astype(np.int64), pars[:, 0].astype(np.int64))

    def layer(self, name): return self.layers[name]


def default_manifest(weights):
    # [layer_param_rom이 rsp_desc로 돌려주는 cfg_desc256의 "원본 소스"]
    # 여기서 만든 op_id/stride/dilation/pad/weight_offset 등이 그대로
    # generated/manifest.json -> (있다면) layer_rom.hex 로 변환되어
    # RTL의 ROM에 박히는 값이 됨. stride 규칙(conv0/conv2/conv4/conv6만 2)이
    # 바로 이 줄에 있음.
    ops = []; h = 256; c = 3
    names = ['features.conv0.conv']
    for i in range(1, 14): names.extend([f'features.conv{i}.depthwise', f'features.conv{i}.pointwise'])
    names += ['heatmap', 'offset']
    for idx, name in enumerate(names):
        w, m, b = weights.layer(name)
        dw = 'depthwise' in name
        s = 2 if name == 'features.conv0.conv' or name in [f'features.conv{i}.depthwise' for i in (2, 4, 6)] else 1
        k = w.shape[-1]; d = 1; p = 1 if k == 3 else 0
        head = name in ('heatmap', 'offset')
        hi = 16 if head else h; ci = 384 if head else c
        ho = (hi + 2 * p - d * (k - 1) - 1) // s + 1
        stage = 0 if idx == 0 else (int(name.split('.')[1][4:]) if not head else 14 + (name == 'offset'))
        mode = name if head else 'body'
        ops.append(dict(op_id=idx, stage_id=int(stage), name=name, kind='dw' if dw else ('conv0' if idx == 0 else 'pw'),
                         hin=hi, win=hi, hout=ho, wout=ho, cin=ci, cout=int(w.shape[0]), kernel=k, stride=s, dilation=d,
                         pad=p, mode=mode, shift=17 if idx == 0 else 16, output_bits=16 if name == 'offset' else 8))
        if not head: h = ho; c = int(w.shape[0])
    return dict(version=VERSION, weights_sha256=weights.sha256,
                geometry_status='IMPLEMENTATION_BASELINE_NOT_CHECKPOINT_ATTESTED',
                geometry_note='dilation=1/padding=1 selected from existing written specification. Run export_geometry.py against actual float model; hardware supports d=1 or 2 without interface change.',
                clock_hz=100000000, w_in=32, w_out=4, input_shape=[256, 256, 3], pixel_order='HWC_RGB',
                rounding='nearest_ties_even', body_scale=[6, 127], heatmap_scale=[6, 127], offset_scale_q16=3096,
                offset_bits=16, threshold_raw=-46, conv0_padding='doubled_raw_neutral_255', ops=ops)


def validate_manifest(man, weights):
    # [하드웨어 대응 없음, 모델 릴리스 게이트] pack 시점에 "이 descriptor들이
    # 서로 앞뒤가 맞는지"(shape 이어짐, accumulator 오버플로 안 남 등)를
    # 미리 걸러내는 PC 전용 체크. RTL은 런타임에 이런 걸 검사하지 않음
    # (fault 신호는 프로토콜 위반만 잡지, 이런 산술적 정합성은 안 잡음).
    if man['version'] != VERSION or man['weights_sha256'] != weights.sha256: raise ValueError('version/hash mismatch')
    if len(man['ops']) != 29: raise ValueError('29 operations required')
    h, w, c = 256, 256, 3
    for idx, op in enumerate(man['ops']):
        qw, m, b = weights.layer(op['name']); head = op['mode'] in ('heatmap', 'offset')
        if head: h, w, c = 16, 16, 384
        if op['op_id'] != idx or (op['hin'], op['win'], op['cin']) != (h, w, c): raise ValueError('graph shape')
        if op['dilation'] not in (1, 2) or op['stride'] not in (1, 2): raise ValueError('geometry unsupported')
        if op['kind'] == 'conv0' and (op['dilation'], op['pad'], op['stride']) != (1, 1, 2): raise ValueError('conv0 requires dilation1/pad1/stride2')
        if qw.shape[0] != op['cout'] or qw.shape[1] != (1 if op['kind'] == 'dw' else c): raise ValueError('weight shape')
        if op['kernel'] != qw.shape[-1] or op['pad'] != (op['dilation'] if op['kernel'] == 3 else 0): raise ValueError('symmetric same padding required')
        ho = (h + 2 * op['pad'] - op['dilation'] * (op['kernel'] - 1) - 1) // op['stride'] + 1
        if (ho, ho) != (op['hout'], op['wout']): raise ValueError('output geometry')
        if op['kind'] == 'dw' and w * ((c + 31) // 32) > 256: raise ValueError('ring buffer exceeds selected geometry capacity')
        lim = 510 if op['kind'] == 'conv0' else 127
        bound = np.abs(qw).reshape(qw.shape[0], -1).sum(1) * lim + np.abs(b) * (2 if op['kind'] == 'conv0' else 1)
        signed_bound(bound, 25 if op['kind'] == 'conv0' else 24, 'accum bound')
        if not head: h, w, c = ho, ho, op['cout']
    return True


def downsample(rgb):
    # [downsample_module 그대로] 레지스터맵의 STRIDE=5(고정), PAD_TOP=280
    # (=이 256해상도 기준으로는 56행, 56*5=280이 원본 해상도 환산값),
    # PAD_LEFT=0 과 정확히 대응됨. 보간 없이 5픽셀마다 하나씩 뽑는
    # decimation이라 회로가 단순함(곱셈기 불필요).
    x = np.asarray(rgb)
    if x.shape != (720, 1280, 3) or x.dtype != np.uint8: raise ValueError('fixed 720x1280 RGB uint8 required')
    out = np.zeros((256, 256, 3), np.uint8)
    out[56:200] = x[::5, ::5]
    return out


def conv_acc(x, w, b, op):
    # [op['kind']에 따라 세 모듈 중 하나로 갈라짐]
    #   'conv0' -> input_conv_pe   (RGB 3채널, 24출력, doubled 입력 단위)
    #   'dw'    -> depthwise_conv_pe (채널별 독립 3x3)
    #   'pw'    -> pointwise_conv_pe (1x1 행렬곱, heatmap/offset 헤드도 이 분기 재사용)
    x = np.asarray(x, dtype=np.int64)
    hi, wi, ci = x.shape; k = op['kernel']; d = op['dilation']; p = op['pad']; s = op['stride']; ho = op['hout']; wo = op['wout']
    conv0 = op['kind'] == 'conv0'
    if conv0: x = 2 * x  # [input_conv_pe 전용] 입력을 2배 단위로 계산(단위 정렬, bias_M 재계산 아님)
    # [line_buffer가 실제로는 "스트리밍 탭"으로 하는 일을, 여기선 numpy
    #  슬라이싱으로 한번에 처리함 — stride=2 게이팅도 xp[...:s] 안에 숨어있음]
    xp = np.pad(x, ((p, p), (p, p), (0, 0)), constant_values=255 if conv0 else 0)  # conv0=doubled raw 255, 나머지는 raw 0
    out = np.zeros((ho, wo, op['cout']), np.int64)
    if op['kind'] == 'dw':
        for ky in range(k):
            for kx in range(k): out += xp[ky*d:ky*d+ho*s:s, kx*d:kx*d+wo*s:s, :] * w[:, 0, ky, kx]
    else:
        for ky in range(k):
            for kx in range(k): out += xp[ky*d:ky*d+ho*s:s, kx*d:kx*d+wo*s:s, :] @ w[:, :, ky, kx].T
    # [세 PE 공통] MAC 누적 완료 후 bias_accum을 그냥 더함 — 곱셈 없음
    # (weight_bram_swap_fsm이 로딩 시점에 이미 bias_M을 곱해서 bias_accum으로 만들어둠)
    out += b * (2 if conv0 else 1)
    signed_bound(out, 25 if conv0 else 24, 'MAC accumulator')  # [하드웨어 대응 없음, PC 전용 assert]
    return out


def forward(image, weights, manifest, dump=None):
    # [top_level_fsm의 "29개 연산을 순서대로 돌린다"는 큰 그림에 대응]
    # 다만 cfg_valid 발행, busy 대기, stage barrier 같은 실제 FSM
    # 상태 전이는 없고, feature_map_io의 DDR 왕복(FM_A/FM_B ping-pong)도
    # 여기선 그냥 파이썬 변수(x, body)로 대체되어 있음 — "값은 맞지만
    # 그 값이 어떻게 이동하는지"는 이 함수가 보증하지 않음.
    validate_manifest(manifest, weights)
    x = np.asarray(image)
    if x.shape != (256, 256, 3) or x.dtype != np.uint8: raise ValueError('256x256 RGB uint8 input')
    body = None; outputs = {}; stats = []
    if dump: Path(dump).mkdir(parents=True, exist_ok=True)
    for op in manifest['ops']:
        w, m, b = weights.layer(op['name'])
        inp = body if op['mode'] in ('heatmap', 'offset') else x
        a = conv_acc(inp, w, b, op)          # -> input_conv_pe / depthwise_conv_pe / pointwise_conv_pe
        xout = requant(a, m, op['shift'], op['mode'])  # -> 각 PE의 재양자화 스테이지
        if op['mode'] == 'body': x = xout; body = xout
        else: outputs[op['mode']] = xout
        stats.append(dict(name=op['name'], shape=list(xout.shape), min=int(xout.min()), max=int(xout.max()),
                           sha256=hashlib.sha256(xout.tobytes()).hexdigest()))
        if dump: np.save(Path(dump) / f"{op['op_id']:02}_{op['name']}.npy", xout)
    return outputs['heatmap'], outputs['offset'], stats


def decode(heat, offset, threshold=-46, offset_m=3096):
    # [앞부분: argmax_threshold] 관절마다 최댓값 위치(grid_row/col)와
    # offset_y/x를 스캔해서 찾음 — argmax_threshold 페이지의
    # "best_score/best_row/best_col/seen" 레지스터 뱅크와 1:1 대응.
    # [뒷부분: coord_restore] grid*80-280 형태의 복원식과 RNE, bounds
    # 체크, canonical-zero 처리, word 패킹까지 — coord_restore 페이지의
    # "offset_scale_Q16=3096" 고정 수치 계약과 JOINT 레지스터
    # (score_raw[31:24], y[23:12], x[11:0]) 비트필드가 여기 그대로 있음.
    if heat.shape != (16, 16, 17) or offset.shape != (16, 16, 34): raise ValueError('head shapes')
    result = []
    for j in range(17):
        n = int(np.argmax(heat[:, :, j].reshape(-1)))  # argmax_threshold: row-major first tie
        r, c = divmod(n, 16); score = int(heat[r, c, j]); oy = int(offset[r, c, j]); ox = int(offset[r, c, j+17])
        # coord_restore: 모델stride16 x 외부stride5 = 80, offset_scale_Q16=3096 x 외부stride5
        x = int(rshift_even(c*80*65536 + ox*offset_m*5, 16))
        y = int(rshift_even((r*80-280)*65536 + oy*offset_m*5, 16))  # -280 = pad_top(56)*외부stride5
        good = score >= threshold and 0 <= x < 1280 and 0 <= y < 720
        xx = x if good else 0; yy = y if good else 0  # invalid는 canonical zero
        word = ((score & 255) << 24) | ((yy & 4095) << 12) | (xx & 4095)  # JOINT_n 레지스터 포맷과 동일
        result.append(dict(joint=j, grid_row=r, grid_col=c, offset_y=oy, offset_x=ox, x=xx, y=yy,
                            score_raw=score, valid=bool(good), word=word))
    return result


def color_markers(rgb, red=(160, 100, 100), blue=(100, 100, 160), min_count=8):
    # [color_marker_detect] downsample_module의 tap 출력(원본 720p 좌표,
    # row는 0,5,...715 만 — 열은 그대로 1280 전체)을 그대로 재현.
    # a[::5,:,:] 가 "행만 5로 건너뛰고 열은 안 건너뛴다"는 게
    # 그 tap 신호(tap_row/tap_col)의 실제 좌표계와 일치.
    a = np.asarray(rgb)[::5, :, :].astype(np.int32)
    masks = [(a[:, :, 0] >= red[0]) & (a[:, :, 1] <= red[1]) & (a[:, :, 2] <= red[2]),
             (a[:, :, 0] <= blue[0]) & (a[:, :, 1] <= blue[1]) & (a[:, :, 2] >= blue[2])]
    out = []
    for mask in masks:
        ys, xs = np.nonzero(mask); n = len(xs); found = n >= min_count
        x = int(xs.sum()) // n if found else 0; y = int((ys * 5).sum()) // n if found else 0
        out.append(dict(found=found, count=n, x=x, y=y, word=(int(found) << 31) | (y << 11) | x))
    return out


def pack_layer(w, m, b, kind):
    """Weights first, then 64-byte aligned parameters. All words little endian.
    PW tiles [out_group,in_batch,out_lane,in_lane], DW [batch,tap,lane].
    conv0 [out_channel,tap,RGB], padded 32 bytes per output channel.
    Params [outgroup,lane] pairs signed32 bias, unsigned32 M; dummy lanes zero.
    """
    # [PC 전용 빌드 스텝이지만, 이 결과 바이트 배치가 그대로
    #  weight_bram_swap_fsm이 DDR에서 읽어 서빙하는 형식이 됨]
    co, ci, kh, kw = w.shape
    if kind == 'pw':
        wp = np.zeros(((co+3)//4, (ci+31)//32, 4, 32), np.int8)
        for o in range(co):
            for i in range(ci): wp[o//4, i//32, o%4, i%32] = w[o, i, 0, 0]
    elif kind == 'dw':
        wp = np.zeros(((co+31)//32, 9, 32), np.int8)
        for o in range(co): wp[o//32, :, o%32] = w[o, 0].reshape(-1)
    else:
        wp = np.zeros((co, 32), np.int8)
        wp[:, :27] = w.transpose(0, 2, 3, 1).reshape(co, 27)
    raw = bytearray(wp.tobytes()); param_offset = (len(raw)+63)//64*64
    raw.extend(bytes(param_offset - len(raw)))
    count = ((co+3)//4)*4
    params = np.zeros((count, 2), dtype='<i4'); params[:co, 0] = b; params[:co, 1] = m
    raw.extend(params.tobytes()); raw.extend(bytes((-len(raw)) % 64))
    return bytes(raw), param_offset


def make_package(archive, out, geometry=None):
    # [PC 전용 빌드 스텝] weights_export.zip -> weights_v4.bin/manifest.json.
    # 이 함수 자체는 하드웨어와 무관하지만, 산출물 두 개는 하드웨어가 직접 씀.
    out = Path(out); out.mkdir(parents=True, exist_ok=True); weights = Weights(archive); man = default_manifest(weights)
    if geometry:
        cfg = json.loads(Path(geometry).read_text())
        for op in man['ops']:
            if op['name'] in cfg['layers']:
                g = cfg['layers'][op['name']]; op.update({k: g[k] for k in ['stride', 'dilation', 'pad']})
        man['geometry_status'] = 'CHECKPOINT_MODULES_EXPORTED'; man['geometry_source'] = cfg.get('source', {})
    validate_manifest(man, weights)
    blob = bytearray()
    for op in man['ops']:
        w, m, b = weights.layer(op['name']); raw, po = pack_layer(w, m, b, op['kind'])
        op['weight_offset'] = len(blob); op['param_offset'] = po; op['dma_bytes'] = len(raw); blob.extend(raw)
    man['packed_sha256'] = hashlib.sha256(blob).hexdigest(); man['packed_bytes'] = len(blob)
    (out / 'weights_v4.bin').write_bytes(blob); (out / 'manifest.json').write_text(json.dumps(man, indent=2, ensure_ascii=False))
    return man


def main():
    # [하드웨어 대응 없음, CLI 진입점]
    p = argparse.ArgumentParser(); sub = p.add_subparsers(dest='cmd', required=True)
    b = sub.add_parser('pack'); b.add_argument('--weights', required=True); b.add_argument('--out', required=True); b.add_argument('--geometry')
    r = sub.add_parser('run'); src = r.add_mutually_exclusive_group(required=True); src.add_argument('--weights'); src.add_argument('--packed')
    r.add_argument('--manifest', required=True); r.add_argument('--image', required=True, help='RGB uint8 .npy 720x1280x3 or 256x256x3')
    r.add_argument('--out', required=True); r.add_argument('--dump', action='store_true')
    a = p.parse_args()
    if a.cmd == 'pack':
        m = make_package(a.weights, a.out, a.geometry); print(json.dumps({'version': VERSION, 'packed_bytes': m['packed_bytes'], 'geometry_status': m['geometry_status']})); return
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True); im = np.load(a.image, allow_pickle=False); markers = None
    if im.shape == (720, 1280, 3): markers = color_markers(im); im = downsample(im)
    m = json.loads(Path(a.manifest).read_text())
    hm, off, stats = forward(im, PackedWeights(a.packed, m) if a.packed else Weights(a.weights), m, out / 'features' if a.dump else None)
    result = decode(hm, off, m['threshold_raw'], m['offset_scale_q16'])
    np.save(out / 'heatmap.npy', hm); np.save(out / 'offset.npy', off)
    (out / 'results.json').write_text(json.dumps({'version': VERSION, 'geometry_status': m['geometry_status'], 'joints': result, 'markers': markers, 'layers': stats}, indent=2))
    print('saved', out)


if __name__ == '__main__': main()
