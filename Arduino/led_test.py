"""led_test.py — Arduino LED 점등 테스트 Python 제어 프로그램

목적:
  아두이노에 시리얼(USB)로 검사 결과(정상/불량)를 보내서
  LED 점등을 테스트하는 프로그램이다.
  10개의 더미 데이터를 JSON 형식으로 생성하고, 2초 간격으로 아두이노에 전송한다.

동작 규칙:
  - 정상(value=1) → 아두이노에 '1' 전송 → LED 안 켜짐
  - 불량(value=0) → 아두이노에 '0' 전송 → LED 3초 점등 후 자동 소멸

더미 데이터 형식 (JSON):
  {"id": 1, "value": 0, "label": "불량"}
  {"id": 2, "value": 1, "label": "정상"}
  ...

사전 설치: pip install pyserial
사용법:    python led_test.py
"""

# serial (pyserial): 아두이노와 USB 시리얼 통신을 하기 위한 라이브러리이다.
# pip install pyserial 로 설치한다.
# 주의: 'import serial'이지만 패키지 이름은 'pyserial'이다.
import serial

# json: 더미 데이터를 JSON 형식(딕셔너리 ↔ 문자열)으로 변환하기 위한 표준 라이브러리이다.
# JSON: JavaScript Object Notation — 데이터를 주고받을 때 쓰는 표준 텍스트 형식이다.
import json

# random: 정상(1)/불량(0)을 랜덤으로 결정하기 위한 표준 라이브러리이다.
# randint(0, 1): 0 또는 1을 랜덤으로 반환한다.
import random

# time: 데이터 전송 간격(2초)을 맞추기 위한 표준 라이브러리이다.
# time.sleep(초): 지정한 시간 동안 프로그램을 일시정지한다.
# time.time(): 현재 시각을 초 단위 소수점으로 반환한다 (경과 시간 계산용).
import time

# sys: 에러 발생 시 프로그램을 종료(sys.exit)하기 위한 표준 라이브러리이다.
import sys


# ============================================================
# 설정값
# ============================================================
# 아두이노가 연결된 시리얼 포트 이름이다.
# Windows: "COM3", "COM4" 등 (장치관리자에서 확인)
# Linux:   "/dev/ttyACM0", "/dev/ttyUSB0" 등
# Mac:     "/dev/cu.usbmodem..." 등
SERIAL_PORT = "COM3"

# 시리얼 통신 속도 (baud rate)이다.
# 아두이노의 Serial.begin(9600)과 같은 값이어야 통신이 된다.
# 단위: bps (bits per second, 초당 전송 비트 수)
BAUD_RATE = 9600

# 생성할 더미 데이터 개수이다.
# 10개의 검사 결과를 시뮬레이션한다.
DATA_COUNT = 10

# 데이터 전송 간격 (초)이다.
# 요구사항: "5초에 한 번씩 넘기는 방식으로 설계"
# 정상이든 불량이든 다음 데이터까지 항상 5초 간격이다.
INTERVAL_SEC = 5

# LED 점등 시간 (초)이다.
# 요구사항: "LED는 점등 후 3초 뒤 자동 소멸"
# 아두이노가 LED를 끄는 데 걸리는 시간과 맞춰야 한다.
LED_ON_DURATION = 3.0


def generate_dummy_data(count: int) -> list[dict]:
    """테스트용 더미 검사 데이터를 생성하는 함수.

    목적:
      실제 AI 검사 결과 대신 랜덤으로 정상/불량 데이터를 만든다.
      각 데이터는 JSON 형식의 딕셔너리로 구성된다.

    매개변수:
      count (int): 생성할 데이터 개수 (예: 10)

    반환값:
      list[dict]: 딕셔너리 리스트. 각 딕셔너리는:
        - "id": 순번 (1부터 시작)
        - "value": 검사 결과 (1=정상, 0=불량)
        - "label": 사람이 읽기 쉬운 상태 문자열 ("정상" 또는 "불량")
    """
    # 빈 리스트를 만든다. 여기에 데이터를 하나씩 추가할 것이다.
    data_list = []

    # 1부터 count까지 반복한다. range(1, 11) → 1, 2, 3, ..., 10
    for i in range(1, count + 1):
        # random.randint(0, 1): 0 또는 1을 50% 확률로 랜덤 선택한다.
        # 0 = 불량(NG), 1 = 정상(OK)
        value = random.randint(0, 1)

        # 딕셔너리(key-value 쌍)를 만들어 리스트에 추가한다.
        data_list.append({
            "id": i,                                    # 순번 (1, 2, 3, ...)
            "value": value,                             # 검사 결과 (0 또는 1)
            "label": "정상" if value == 1 else "불량"    # 사람이 읽을 수 있는 상태
            # 위 줄은 삼항 연산자: 조건 if True else 값 if False
        })

    # 완성된 데이터 리스트를 반환한다.
    return data_list


def main():
    """메인 함수: 더미 데이터를 생성하고, 아두이노에 시리얼로 전송한다.

    실행 순서:
      1. 더미 데이터 10개 생성 (랜덤 정상/불량)
      2. 아두이노 시리얼 포트 연결
      3. 아두이노 초기화 대기 (2초)
      4. 2초 간격으로 데이터 전송 + LED 점등 확인
      5. 전송 완료 후 시리얼 포트 닫기

    매개변수: 없음
    반환값: 없음
    """

    # ── 1. 더미 데이터 생성 ──
    # DATA_COUNT(10)개의 랜덤 검사 데이터를 만든다.
    dummy_data = generate_dummy_data(DATA_COUNT)

    # 테스트 시작 안내를 콘솔에 출력한다.
    print("=" * 60)
    print("  Arduino LED 점등 테스트")
    print("  정상(1): LED OFF  |  불량(0): LED 3초 점등")
    print(f"  데이터 전송 간격: {INTERVAL_SEC}초")
    print("=" * 60)

    # 생성된 더미 데이터를 JSON 형식으로 보기 좋게 출력한다.
    # json.dumps(): 딕셔너리 → JSON 문자열로 변환하는 함수이다.
    # ensure_ascii=False: 한글("정상", "불량")이 깨지지 않게 한다.
    # indent=2: 들여쓰기 2칸으로 보기 좋게 포맷한다.
    print(f"\n생성된 더미 데이터 ({DATA_COUNT}개):")
    print(json.dumps(dummy_data, ensure_ascii=False, indent=2))
    print()

    # ── 2. 시리얼 포트 연결 ──
    # 아두이노가 USB로 연결된 시리얼 포트를 연다.
    try:
        # serial.Serial(): 시리얼 포트를 열고 통신 객체를 생성한다.
        # timeout=2: 응답 대기 최대 2초 (2초 안에 응답 없으면 빈 값 반환)
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=2)
        print(f"[연결 성공] 포트: {SERIAL_PORT}")
    except serial.SerialException as e:
        # 포트가 없거나 다른 프로그램이 사용 중이면 에러가 발생한다.
        print(f"[연결 실패] {e}")
        print("  → 아두이노가 연결되어 있는지, 포트 번호가 맞는지 확인하세요.")
        print(f"  → 현재 설정: SERIAL_PORT = \"{SERIAL_PORT}\"")
        # sys.exit(1): 에러 코드 1로 프로그램을 종료한다 (0=정상, 1=에러).
        sys.exit(1)

    # ── 3. 아두이노 리셋 대기 ──
    # 시리얼 포트를 열면 아두이노가 자동으로 리셋(재시작)된다.
    # setup() 함수가 실행 완료될 때까지 2초 대기한다.
    # 이 대기 없이 바로 데이터를 보내면 아두이노가 받지 못할 수 있다.
    print("[대기중] 아두이노 초기화 (2초)...\n")
    time.sleep(2)

    # 시리얼 수신/송신 버퍼를 모두 비운다.
    # 아두이노가 초기화 중 보낸 "Arduino Ready" 메시지와
    # 이전 실행에서 남은 찌꺼기 데이터를 모두 제거한다.
    ser.reset_input_buffer()   # 수신 버퍼 비우기 (아두이노 → PC)
    ser.reset_output_buffer()  # 송신 버퍼 비우기 (PC → 아두이노)

    # 혹시 남아있는 데이터를 한 번 더 읽어서 버린다.
    while ser.in_waiting > 0:
        ser.readline()

    # ── 4. 데이터 순차 전송 (5초 간격) ──
    # 테이블 헤더를 출력한다. 콘솔에서 결과를 보기 쉽게 정렬한다.
    # 시간 컬럼을 추가하여 각 전송 시각을 기록한다.
    print(f"{'순번':>4} | {'시간':>10} | {'상태':>6} | {'JSON 데이터':<40} | 아두이노 응답")
    print("-" * 95)

    # enumerate(): 리스트 순회 시 인덱스(idx)와 값(item)을 동시에 가져온다.
    # idx: 0부터 시작하는 인덱스 (마지막 데이터인지 확인할 때 사용)
    # item: 더미 데이터 딕셔너리 하나 ({"id": 1, "value": 0, "label": "불량"})
    for idx, item in enumerate(dummy_data):
        # 이번 전송 시작 시각을 기록한다. 정확한 5초 간격을 맞출 때 사용한다.
        send_time = time.time()

        # 전송 시점의 시각을 바로 기록한다 (응답 대기 전에 찍어야 정확).
        # time.strftime(): 시간을 "HH:MM:SS" 형식 문자열로 변환한다.
        timestamp = time.strftime("%H:%M:%S")

        # 딕셔너리를 JSON 문자열로 변환한다 (콘솔 출력용).
        json_str = json.dumps(item, ensure_ascii=False)

        # 상태 텍스트를 결정한다.
        icon = "정상(1)" if item["value"] == 1 else "불량(0)"

        # ── 로그를 먼저 출력한다 ──
        # LED가 켜지기 전에 콘솔에 결과를 먼저 표시한다.
        # 이렇게 해야 "로그 출력 → LED 점등" 순서가 보장된다.
        print(f"  {item['id']:>2} | {timestamp} | {icon} | {json_str:<40}", end="", flush=True)
        # end="": 줄바꿈 없이 같은 줄에 이어서 출력할 준비를 한다.
        # flush=True: 버퍼에 쌓지 말고 즉시 화면에 출력한다.

        # ── 아두이노에 검사 결과 전송 (로그 출력 후) ──
        # item["value"]는 정수(0 또는 1)이므로, str()로 문자열로 변환한다.
        # .encode("utf-8"): 문자열을 바이트로 변환한다 (시리얼은 바이트를 전송).
        # 아두이노는 '0' 또는 '1' 문자를 받아서 LED를 제어한다.
        ser.write(str(item["value"]).encode("utf-8"))

        # ── 아두이노 응답 대기 ──
        # 불량(0)이면 아두이노가 LED를 켜고 3초 후 끄므로,
        # "NG_ON"과 "NG_OFF" 두 응답을 모두 받을 때까지 기다린다.
        # 정상(1)이면 즉시 "OK" 응답이 오므로 짧게 대기한다.
        # 정상/불량 모두 LED가 3초간 점등되므로 동일하게 대기한다.
        # LED 3초 점등 + 응답 전송 시간 여유 → 3.3초 대기
        time.sleep(LED_ON_DURATION + 0.3)

        # ── 아두이노 응답 읽기 ──
        # 시리얼 수신 버퍼에 쌓인 응답을 모두 읽는다.
        # 유효한 응답("OK", "NG_ON", "NG_OFF")만 필터링한다.
        valid_responses = {"OK", "NG_ON", "NG_OFF"}  # 유효한 응답 목록
        responses = []
        # ser.in_waiting: 수신 버퍼에 아직 읽지 않은 바이트 수
        while ser.in_waiting > 0:
            # readline(): 줄바꿈(\n)까지 한 줄을 읽는다.
            # .decode("utf-8"): 바이트를 문자열로 변환한다.
            # errors="ignore": 변환 불가능한 문자는 무시한다.
            # .strip(): 앞뒤 공백과 줄바꿈을 제거한다.
            line = ser.readline().decode("utf-8", errors="ignore").strip()
            # 유효한 응답만 리스트에 추가한다 ("Arduino Ready" 등 불필요한 메시지 제외).
            if line in valid_responses:
                responses.append(line)

        # 응답들을 " → "로 연결하여 같은 줄 뒤에 이어서 출력한다.
        response_text = " → ".join(responses) if responses else "-"

        # 아두이노 응답을 줄 끝에 추가하고 줄바꿈한다.
        print(f" | {response_text}")

        # ── 다음 전송까지 정확히 5초 간격 유지 ──
        # 정상이든 불량이든 전송 시작 기준으로 항상 5초 간격을 유지한다.
        # 마지막 데이터가 아닐 때만 대기한다.
        if idx < len(dummy_data) - 1:
            # 이번 전송에 걸린 시간을 계산한다.
            elapsed = time.time() - send_time
            # 2초에서 이미 경과한 시간을 빼면 남은 대기 시간이 된다.
            # 예: 불량 처리에 1.3초 걸렸으면 → 2.0 - 1.3 = 0.7초만 더 대기
            remaining = INTERVAL_SEC - elapsed
            # 남은 시간이 0보다 크면 그만큼 대기한다.
            if remaining > 0:
                time.sleep(remaining)

    # ── 5. 종료 ──
    # 시리얼 포트를 닫는다. 열어둔 포트를 닫지 않으면 다른 프로그램이 사용할 수 없다.
    ser.close()
    print("-" * 80)
    print("\n[완료] 테스트 종료.")


# 이 파일이 직접 실행될 때만 main() 함수를 호출한다.
# 다른 파일에서 import할 때는 main()이 자동 실행되지 않는다.
# __name__: 파이썬이 자동으로 설정하는 특수 변수이다.
#   직접 실행 시: "__main__"
#   import 시: 모듈 이름 (예: "led_test")
if __name__ == "__main__":
    main()
