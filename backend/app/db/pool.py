from __future__ import annotations

import logging

import asyncpg

from app.core.config import settings

logger = logging.getLogger(__name__)


class DatabasePool:
    """asyncpg 连接池，全局单例。"""

    def __init__(self) -> None:
        self._pool: asyncpg.Pool | None = None

    async def init(self) -> None:
        self._pool = await asyncpg.create_pool(
            dsn=settings.pg_dsn,
            min_size=settings.pg_pool_min,
            max_size=settings.pg_pool_max,
        )
        logger.info("DatabasePool initialized", min=settings.pg_pool_min, max=settings.pg_pool_max)

    async def close(self) -> None:
        if self._pool is not None:
            await self._pool.close()
            self._pool = None

    async def fetch(self, sql: str, *args) -> list[asyncpg.Record]:
        assert self._pool is not None
        return await self._pool.fetch(sql, *args)

    async def fetchrow(self, sql: str, *args) -> asyncpg.Record | None:
        assert self._pool is not None
        return await self._pool.fetchrow(sql, *args)

    async def execute(self, sql: str, *args) -> str:
        assert self._pool is not None
        return await self._pool.execute(sql, *args)

    async def executemany(self, sql: str, args: list[tuple]) -> None:
        assert self._pool is not None
        await self._pool.executemany(sql, args)

    def pool(self) -> asyncpg.Pool:
        assert self._pool is not None
        return self._pool
