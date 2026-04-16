"""Station2Main.py
조립 검사 (Station 2) AI 추론 서버 진입점.

YOLO11 + PatchCore 하이브리드 추론으로 캡/라벨 조립 완성도를 검사한다.
- 1차: YOLO11 — cap, label, liquid_level 존재 여부 + IoU 기반 위치 판정
- 2차: PatchCore — 라벨 표면 품질 이상탐지
- 둘 중 하나라도 NG이면 최종 NG

실행:
  cd Factory/AiServer
  python -m Station2.Station2Main
"""

from __future__ import annotations

import asyncio
import logging
import signal

from Common.Config import StationConfig
from Common.Inferencer import Station2Inferencer
from Common.StationRunner import StationRunner


logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)


async def main() -> None:
    config = StationConfig(
        station_id=2,
        main_server_host="127.0.0.1",
        main_server_port=9000,
        camera_serial="",
        model_path="./models/station2_yolo11.pt",               # YOLO11 모델
        patchcore_model_path="./models/station2_patchcore.ckpt", # 라벨 표면 PatchCore
        anomaly_threshold=0.5,             # PatchCore 이상 점수 임계값
        yolo_conf_threshold=0.5,           # YOLO 탐지 신뢰도 임계값
        yolo_iou_threshold=0.45,           # YOLO NMS IoU 임계값
        yolo_input_size=640,               # YOLO 입력 크기
        patchcore_input_size=224,          # PatchCore 입력 크기
        grab_queue_max=16,
        inference_workers=1,
        sender_workers=1,
        arduino_port=None,                 # 예: "COM4" 또는 "/dev/ttyUSB1"
        arduino_baud=9600,
    )

    inferencer = Station2Inferencer(config)
    runner = StationRunner(config, inferencer)

    loop = asyncio.get_running_loop()
    stop_event = asyncio.Event()

    def request_stop() -> None:
        stop_event.set()

    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, request_stop)
        except NotImplementedError:
            pass

    runner_task = loop.create_task(runner.run())
    await stop_event.wait()
    await runner.stop()
    await runner_task


if __name__ == "__main__":
    asyncio.run(main())
