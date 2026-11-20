from __future__ import annotations

from app.db.pool import DatabasePool


class DeviceRepository:
    """base_device_base_info 设备基础数据 SQL 访问层。"""

    def __init__(self, pool: DatabasePool) -> None:
        self.pool = pool

    async def list(self, page: int, size: int, keyword: str | None = None) -> tuple[list[dict], int]:
        where = ""
        args: list = []
        if keyword:
            where = "WHERE device_code ILIKE $1 OR device_name ILIKE $1"
            args.append(f"%{keyword}%")
        rows = await self.pool.fetch(
            f"SELECT * FROM base_device_base_info {where} ORDER BY id DESC LIMIT ${len(args) + 1} OFFSET ${len(args) + 2}",
            *args, size, (page - 1) * size,
        )
        total_row = await self.pool.fetchrow(
            f"SELECT COUNT(*) AS cnt FROM base_device_base_info {where}", *args,
        )
        return [dict(r) for r in rows], int(total_row["cnt"])

    async def get(self, device_id: int) -> dict | None:
        row = await self.pool.fetchrow("SELECT * FROM base_device_base_info WHERE id = $1", device_id)
        return dict(row) if row else None

    async def get_by_code(self, device_code: str) -> dict | None:
        row = await self.pool.fetchrow("SELECT * FROM base_device_base_info WHERE device_code = $1", device_code)
        return dict(row) if row else None

    async def all(self) -> list[dict]:
        rows = await self.pool.fetch("SELECT * FROM base_device_base_info ORDER BY id")
        return [dict(r) for r in rows]

    async def create(self, data: dict) -> dict:
        cols = ", ".join(data.keys())
        placeholders = ", ".join(f"${i + 1}" for i in range(len(data)))
        row = await self.pool.fetchrow(
            f"INSERT INTO base_device_base_info ({cols}) VALUES ({placeholders}) RETURNING *",
            *data.values(),
        )
        return dict(row)

    async def update(self, device_id: int, data: dict) -> dict | None:
        sets = ", ".join(f"{k} = ${i + 1}" for i, k in enumerate(data.keys()))
        vals = list(data.values()) + [device_id]
        row = await self.pool.fetchrow(
            f"UPDATE base_device_base_info SET {sets} WHERE id = ${len(vals)} RETURNING *",
            *vals,
        )
        return dict(row) if row else None

    async def delete(self, device_id: int) -> bool:
        result = await self.pool.execute("DELETE FROM base_device_base_info WHERE id = $1", device_id)
        return "DELETE 1" in result
