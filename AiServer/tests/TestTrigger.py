"""
test_trigger.py
TrainingMain.py(학습 서버)가 TCP 통신을 제대로 수신하고 
YOLO 학습 파이프라인을 정상적으로 구동하는지 확인하는 통합 테스트 스크립트.
"""
import asyncio
import sys
import json
import struct
from pathlib import Path

# AiServer 최상위 경로를 sys.path에 추가하여 Common 패키지를 합법적으로 로드
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from Common.Protocol import ProtocolNo
from Common.Packet import PacketBuilder

async def run_test():
    host = '10.10.10.120'
    port = 9100  # 학습 서버 포트

    # 1. 메인 서버가 보낼 규격과 똑같이 데이터 구성
    # 실제 데이터 폴더 절대 경로 (학습서버가 열 수 있어야 함).
    # 주의: 이 경로는 "학습서버 PC 의 로컬 절대경로" 여야 한다 —
    #   본 스크립트를 어디서 실행하든 학습서버가 파일을 열 때 쓰는 경로이기 때문.
    # 학습서버(10.10.10.120, 네이티브 Ubuntu) 로컬:
    target_data_path = "/mnt/hdd/factory/code/AiServer/data/station2/yolo"
    
    body = {
        "request_id": "test_req_001",
        "station_id": 2,
        "model_type": "YOLO11",
        "data_path": target_data_path
    }

    # 2. PacketBuilder 사용 (직접 바이너리 조작 최소화)
    packet = PacketBuilder.build_packet(
        protocol_no=int(ProtocolNo.TRAIN_START_REQ), # 매직넘버 1100 대신 Enum 사용
        body_dict=body,
        request_id="test_req_001"
    )

    print(f" [테스트] 학습 서버({host}:{port})로 TRAIN_START_REQ 전송 시도...")

    try:
        # socket.socket() 직접 호출 금지 규칙에 따라 asyncio.open_connection 사용
        reader, writer = await asyncio.open_connection(host, port)
        
        writer.write(packet)
        await writer.drain()
        print(f" [테스트] 전송 완료! (요청 경로: {target_data_path})")

        # 3. 학습 서버의 수락 응답(1101) 대기
        header = await reader.readexactly(4)
        json_size = struct.unpack(">I", header)[0]
        res_body = await reader.readexactly(json_size)
        
        msg = json.loads(res_body.decode("utf-8"))
        
        if msg.get("protocol_no") == int(ProtocolNo.TRAIN_START_RES):
            print(f" [테스트] 학습 서버 응답 수신: {msg.get('message')}")
            print(" 이제 TrainingMain.py 터미널을 확인하세요! 학습이 시작되었어야 합니다.")
        else:
            print(f" [테스트] 예상치 못한 응답: {msg}")

        writer.close()
        await writer.wait_closed()

    except ConnectionRefusedError:
        print(" [테스트 실패] 학습 서버에 연결할 수 없습니다. TrainingMain.py가 실행 중인지 확인하세요.")
    except Exception as e:
        print(f" [테스트 에러] {e}")

if __name__ == "__main__":
    asyncio.run(run_test())