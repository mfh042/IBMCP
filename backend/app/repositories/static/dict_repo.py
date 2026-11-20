from __future__ import annotations

from app.db.pool import DatabasePool


class GenericDictRepository:
    """静态字典通用仓储（category / brand / port / protocol 等）。"""

    def __init__(self, pool: DatabasePool, table: str, code_col: str = "code", name_col: str = "name") -> None:
        self.pool = pool
        self.table = table
        self.code_col = code_col
        self.name_col = name_col

    async def list(self, page: int, size: int, keyword: str | None = None) -> tuple[list[dict], int]:
        where = ""
        args: list = []
        if keyword:
            where = f"WHERE {self.code_col} ILIKE $1 OR {self.name_col} ILIKE $1"
            args.append(f"%{keyword}%")
        rows = await self.pool.fetch(
            f"SELECT * FROM {self.table} {where} ORDER BY id DESC LIMIT ${len(args) + 1} OFFSET ${len(args) + 2}",
            *args, size, (page - 1) * size,
        )
        total_row = await self.pool.fetchrow(f"SELECT COUNT(*) AS cnt FROM {self.table} {where}", *args)
        return [dict(r) for r in rows], int(total_row["cnt"])

    async def all(self) -> list[dict]:
        rows = await self.pool.fetch(f"SELECT * FROM {self.table} ORDER BY id")
        return [dict(r) for r in rows]

    async def get(self, row_id: int) -> dict | None:
        row = await self.pool.fetchrow(f"SELECT * FROM {self.table} WHERE id = $1", row_id)
        return dict(row) if row else None

    async def create(self, data: dict) -> dict:
        cols = ", ".join(data.keys())
        placeholders = ", ".join(f"${i + 1}" for i in range(len(data)))
        row = await self.pool.fetchrow(
            f"INSERT INTO {self.table} ({cols}) VALUES ({placeholders}) RETURNING *", *data.values(),
        )
        return dict(row)

    async def update(self, row_id: int, data: dict) -> dict | None:
        sets = ", ".join(f"{k} = ${i + 1}" for i, k in enumerate(data.keys()))
        vals = list(data.values()) + [row_id]
        row = await self.pool.fetchrow(
            f"UPDATE {self.table} SET {sets} WHERE id = ${len(vals)} RETURNING *",
            *vals,
        )
        return dict(row) if row else None

    async def delete(self, row_id: int) -> bool:
        result = await self.pool.execute(f"DELETE FROM {self.table} WHERE id = $1", row_id)
        return "DELETE 1" in result
