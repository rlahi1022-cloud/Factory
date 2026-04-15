"""Station2Main.py
조립 검사 (Station 2) AI 추론 서버 진입점.
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
        model_path="./models/station2_yolo11.pt",
        grab_queue_max=16,
        inference_workers=1,
        sender_workers=1,
        arduino_port=None,
    )

    inferencer = Station2Inferencer()
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
