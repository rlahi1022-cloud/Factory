"""ConfigLoader.py — config/config.json 통합 설정 로더

프로젝트 루트의 config/config.json을 읽어 AiServer 전체에서 공유한다.
점(.) 구분 키로 중첩 값을 조회할 수 있다.

사용법:
    from Common.ConfigLoader import ConfigLoader
    ConfigLoader.load()  # 기본 경로 또는 CONFIG_PATH 환경변수
    host = ConfigLoader.get("network.main_server_host")
    port = ConfigLoader.get_int("network.main_server_ai_port")

우선순위:
    명령줄 인자 > CONFIG_PATH 환경변수 > 기본값(../config/config.json)
"""

from __future__ import annotations

import json
import logging
import os
import sys
from pathlib import Path
from typing import Any, Optional

logger = logging.getLogger(__name__)


class ConfigLoader:
    """통합 설정 로더 (클래스 메서드 기반 싱글톤)."""

    _config: Optional[dict] = None
    _path: Optional[Path] = None

    @classmethod
    def load(cls, path: Optional[str] = None) -> None:
        """config.json 파일을 로드한다.

        Args:
            path: 명시적 경로. None이면 환경변수/기본 경로 탐색.
        """
        # 경로 우선순위: 인자 > 환경변수 > 프로젝트 상대 경로 탐색
        resolved: Optional[Path] = None
        if path:
            resolved = Path(path)
        elif env := os.getenv("CONFIG_PATH"):
            resolved = Path(env)
        else:
            # 현재 파일(AiServer/Common/ConfigLoader.py) 기준 프로젝트 루트 탐색
            here = Path(__file__).resolve()
            # Factory/AiServer/Common/ → Factory/config/config.json
            candidates = [
                here.parent.parent.parent / "config" / "config.json",
                Path.cwd() / "config" / "config.json",
                Path("../config/config.json"),
            ]
            for c in candidates:
                if c.exists():
                    resolved = c
                    break

        if not resolved or not resolved.exists():
            logger.error("config.json을 찾을 수 없습니다")
            raise FileNotFoundError("config.json not found")

        with open(resolved, "r", encoding="utf-8") as f:
            cls._config = json.load(f)
        cls._path = resolved
        logger.info("config 로드 완료: %s", resolved)

    @classmethod
    def _get(cls, key: str, default: Any = None) -> Any:
        """점 구분 키로 중첩 값 조회."""
        if cls._config is None:
            raise RuntimeError("ConfigLoader.load()를 먼저 호출하세요")
        value: Any = cls._config
        for k in key.split("."):
            if isinstance(value, dict) and k in value:
                value = value[k]
            else:
                return default
        return value

    @classmethod
    def get(cls, key: str, default: str = "") -> str:
        v = cls._get(key, default)
        return str(v) if v is not None else default

    @classmethod
    def get_int(cls, key: str, default: int = 0) -> int:
        v = cls._get(key, default)
        try:
            return int(v)
        except (TypeError, ValueError):
            return default

    @classmethod
    def get_float(cls, key: str, default: float = 0.0) -> float:
        v = cls._get(key, default)
        try:
            return float(v)
        except (TypeError, ValueError):
            return default

    @classmethod
    def get_bool(cls, key: str, default: bool = False) -> bool:
        v = cls._get(key, default)
        if isinstance(v, bool):
            return v
        if isinstance(v, str):
            return v.lower() in ("true", "1", "yes")
        return default

    @classmethod
    def get_list(cls, key: str) -> list:
        v = cls._get(key, [])
        return v if isinstance(v, list) else []

    @classmethod
    def source_path(cls) -> Optional[Path]:
        return cls._path
