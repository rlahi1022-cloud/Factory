"""Protocol.py
C++ MainServer/common/Protocol.h 와 동기화된 메시지 번호 enum.
양 서버는 같은 wire 정수값을 사용해야 한다.
"""

from __future__ import annotations

from enum import IntEnum


PROTOCOL_VERSION = "1.0"


class ProtocolNo(IntEnum):
    # ===== 외부 100~199 (MFC ↔ 운용) — 번호만 예약 =====
    LOGIN_REQ              = 100
    LOGIN_RES              = 101
    LOGOUT_REQ             = 102
    LOGOUT_RES             = 103
    INSPECT_NG_PUSH        = 110
    INSPECT_NG_ACK_EXT     = 111
    INSPECT_OK_COUNT_PUSH  = 112
    INSPECT_HISTORY_REQ    = 114
    INSPECT_HISTORY_RES    = 115
    STATS_REQ              = 130
    STATS_RES              = 131
    MODEL_LIST_REQ         = 150
    MODEL_LIST_RES         = 151
    RETRAIN_REQ            = 152
    RETRAIN_RES            = 153
    RETRAIN_PROGRESS_PUSH  = 154
    MODEL_DEPLOY_NOTIFY    = 156
    MODEL_DEPLOY_ACK_EXT   = 157
    SERVER_HEALTH_PUSH     = 170
    EXT_ACK                = 190
    EXT_NACK               = 191
    EXT_ERROR              = 192

    # ===== 내부 1000~1999 (운용 ↔ 추론/학습) — 본 단계 구현 대상 =====
    STATION1_NG            = 1000
    STATION1_NG_ACK        = 1001
    STATION2_NG            = 1002
    STATION2_NG_ACK        = 1003
    STATION_OK_COUNT       = 1004
    INSPECT_META           = 1006

    MODEL_RELOAD_CMD       = 1010
    MODEL_RELOAD_RES       = 1011

    TRAIN_START_REQ        = 1100
    TRAIN_START_RES        = 1101
    TRAIN_PROGRESS         = 1102
    TRAIN_COMPLETE         = 1104
    TRAIN_COMPLETE_ACK     = 1105
    TRAIN_FAIL             = 1106
    TRAIN_FAIL_ACK         = 1107

    HEALTH_PING            = 1200
    HEALTH_PONG            = 1201
    QUEUE_STATUS           = 1210
    INFERENCE_TIMEOUT      = 1212

    ARDUINO_REJECT         = 1300
    ARDUINO_ALERT          = 1302

    INTERNAL_ACK           = 1900
    INTERNAL_NACK          = 1901
    INTERNAL_RETRY_REQ     = 1902
    INTERNAL_RETRY_DATA    = 1903
    INTERNAL_ERROR         = 1904


# ACK 필수 메시지 집합
ACK_REQUIRED_NOS: frozenset[int] = frozenset({
    ProtocolNo.STATION1_NG,
    ProtocolNo.STATION2_NG,
    ProtocolNo.MODEL_RELOAD_CMD,
    ProtocolNo.TRAIN_COMPLETE,
    ProtocolNo.TRAIN_FAIL,
    ProtocolNo.INSPECT_NG_PUSH,
    ProtocolNo.MODEL_DEPLOY_NOTIFY,
})


def requires_ack(protocol_no: int) -> bool:
    return protocol_no in ACK_REQUIRED_NOS


# NG 송신 → 기대 ACK 번호 매핑
_ACK_NO_MAP: dict[int, int] = {
    ProtocolNo.STATION1_NG: ProtocolNo.STATION1_NG_ACK,
    ProtocolNo.STATION2_NG: ProtocolNo.STATION2_NG_ACK,
}


def expected_ack_no(send_no: int) -> int:
    return _ACK_NO_MAP.get(send_no, ProtocolNo.INTERNAL_ACK)
