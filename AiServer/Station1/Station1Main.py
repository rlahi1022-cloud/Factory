"""Station1Main.py
입고 검사 (Station 1) AI 추론 서버 진입점.

PatchCore 이상탐지로 빈 페트병(삼다수) 외관 결함을 탐지한다.
결함 유형: 크랙, 이물질, 오염, 파손 등.

실행:
  cd Factory/AiServer
  python -m Station1.Station1Main
"""

from __future__ import annotations

import asyncio
import logging
import signal

from Common.Config import StationConfig
from Common.Inferencer import Station1Inferencer
from Common.StationRunner import StationRunner


logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)


async def main() -> None:
    config = StationConfig(
        station_id=1,
        main_server_host="127.0.0.1",
        main_server_port=9000,
        camera_serial="",
        model_path="./models/station1_patchcore.ckpt",
        patchcore_model_path="",           # Station1은 사용 안 함
        anomaly_threshold=0.5,             # PatchCore 이상 점수 임계값
        patchcore_input_size=224,
        grab_queue_max=16,
        inference_workers=1,
        sender_workers=1,
        arduino_port=None,                 # 예: "COM3" 또는 "/dev/ttyUSB0"
        arduino_baud=9600,
    )

    inferencer = Station1Inferencer(config)
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
