"""Station1Main.py
입고 검사 (Station 1) AI 추론 서버 진입점.
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
        grab_queue_max=16,
        inference_workers=1,
        sender_workers=1,
        arduino_port=None,  # 예: "/dev/ttyUSB0" or "COM3"
    )

    inferencer = Station1Inferencer()
    runner = StationRunner(config, inferencer)

    loop = asyncio.get_running_loop()
    stop_event = asyncio.Event()

    def request_stop() -> None:
        stop_event.set()

    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, request_stop)
        except NotImplementedError:
            # Windows에서는 signal handler 등록 불가 — 무시
            pass

    runner_task = loop.create_task(runner.run())
    await stop_event.wait()
    await runner.stop()
    await runner_task


if __name__ == "__main__":
    asyncio.run(main())
