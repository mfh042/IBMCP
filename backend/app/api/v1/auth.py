from __future__ import annotations

from datetime import datetime, timezone
from typing import Any

from fastapi import APIRouter, Depends, Query
from pydantic import BaseModel, Field

from app.core.deps import get_db_pool, require_login
from app.core.errors import BizError
from app.core.security import (
    create_access_token,
    hash_password,
    verify_password,
)
from app.db.pool import DatabasePool

router = APIRouter()


def _gen_refresh_token(uid: str, username: str) -> str:
    return create_access_token(
        sub=uid,
        extra={"username": username, "scope": "refresh"},
    )


class LoginReq(BaseModel):
    username: str = Field(min_length=1, max_length=64)
    password: str = Field(min_length=1, max_length=256)


class UserCreateReq(BaseModel):
    username: str
    display_name: str | None = None
    password: str = Field(min_length=6, max_length=256)
    roles: list[str] = Field(default_factory=list)
    enabled: bool = True
    protected: bool = False


class UserUpdateReq(BaseModel):
    display_name: str | None = None
    roles: list[str] | None = None
    enabled: bool | None = None
    protected: bool | None = None


class PasswordChangeReq(BaseModel):
    old_password: str
    new_password: str = Field(min_length=6, max_length=256)


class RolesUpdateReq(BaseModel):
    roles: list[str]


class RoleCreateReq(BaseModel):
    role_code: str = Field(min_length=1, max_length=64)
    role_name: str = Field(min_length=1, max_length=128)
    description: str | None = None


class RoleUpdateReq(BaseModel):
    role_name: str | None = None
    description: str | None = None


@router.post("/login")
async def login(body: LoginReq, pool: DatabasePool = Depends(get_db_pool)) -> dict:
    row = await pool.fetchrow(
        "SELECT id, username, display_name, password_hash, roles, enabled FROM sys_users WHERE username = $1",
        body.username,
    )
    if row is None or not row["enabled"]:
        raise BizError(code=1001, message="invalid credentials")
    if not verify_password(body.password, row["password_hash"]):
        raise BizError(code=1001, message="invalid credentials")

    roles = row["roles"] or []
    token = create_access_token(sub=row["id"], extra={"username": row["username"], "roles": roles})
    refresh = _gen_refresh_token(str(row["id"]), row["username"])

    return {
        "token": token,
        "refresh_token": refresh,
        "id": int(row["id"]),
        "username": row["username"],
        "display_name": row["display_name"] or row["username"],
        "roles": roles,
    }


@router.post("/refresh")
async def refresh(body: dict, pool: DatabasePool = Depends(get_db_pool)) -> dict:
    from app.core.security import decode_token

    token = body.get("refresh_token", "")
    payload = decode_token(token)
    if payload is None or payload.get("scope") != "refresh":
        raise BizError(code=401, message="invalid refresh token")

    uid = int(payload["sub"])
    row = await pool.fetchrow("SELECT id, username, roles, enabled FROM sys_users WHERE id = $1", uid)
    if row is None or not row["enabled"]:
        raise BizError(code=1002, message="user disabled or not found")

    roles = row["roles"] or []
    new_token = create_access_token(sub=row["id"], extra={"username": row["username"], "roles": roles})
    return {"token": new_token}


@router.get("/verify")
async def verify(user: dict = Depends(require_login)) -> dict:
    return {
        "valid": True,
        "id": int(user.get("sub", 0)),
        "username": user.get("username"),
        "roles": user.get("roles", []),
    }


@router.get("/me")
async def me(user: dict = Depends(require_login)) -> dict:
    uid = int(user.get("sub", 0))
    pool: DatabasePool = __import__("app.main", fromlist=["db_pool"]).db_pool
    row = await pool.fetchrow("SELECT id, username, display_name, roles FROM sys_users WHERE id = $1", uid)
    if row is None:
        raise BizError(code=404, message="user not found")
    return {
        "uid": int(row["id"]),
        "username": row["username"],
        "display_name": row["display_name"] or row["username"],
        "roles": row["roles"] or [],
    }


@router.get("/users")
async def list_users(
        page: int = Query(1, ge=1),
        size: int = Query(20, ge=1, le=200),
        keyword: str | None = None,
        pool: DatabasePool = Depends(get_db_pool),
        user: dict = Depends(require_login),
) -> dict:
    where = ""
    args: list[Any] = []
    if keyword:
        where = "WHERE username ILIKE $1 OR display_name ILIKE $1"
        args.append(f"%{keyword}%")
    rows = await pool.fetch(
        f"SELECT id, username, display_name, roles, enabled, protected, created_at, updated_at "
        f"FROM sys_users {where} ORDER BY id LIMIT ${len(args) + 1} OFFSET ${len(args) + 2}",
        *args,
        size,
        (page - 1) * size,
    )
    total_row = await pool.fetchrow(
        f"SELECT COUNT(*) AS cnt FROM sys_users {where}", *args
    )
    return {"items": [dict(r) for r in rows], "total": int(total_row["cnt"])}


@router.get("/users/{uid}")
async def get_user(uid: int, pool: DatabasePool = Depends(get_db_pool), user: dict = Depends(require_login)) -> dict:
    row = await pool.fetchrow(
        "SELECT id, username, display_name, roles, enabled, protected, created_at, updated_at "
        "FROM sys_users WHERE id = $1",
        uid,
    )
    if row is None:
        raise BizError(code=404, message="user not found")
    return dict(row)


@router.post("/users")
async def create_user(body: UserCreateReq, pool: DatabasePool = Depends(get_db_pool),
                      user: dict = Depends(require_login)) -> dict:
    if "admin" not in (user.get("roles") or []):
        raise BizError(code=403, message="admin required")
    exist = await pool.fetchrow("SELECT 1 FROM sys_users WHERE username = $1", body.username)
    if exist:
        raise BizError(code=409, message="username already exists")
    now = int(datetime.now(timezone.utc).timestamp())
    row = await pool.fetchrow(
        "INSERT INTO sys_users (username, display_name, password_hash, roles, enabled, protected, created_at, updated_at) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $7) RETURNING id, username, display_name, roles, enabled, protected",
        body.username,
        body.display_name or body.username,
        hash_password(body.password),
        body.roles,
        body.enabled,
        body.protected,
        now,
    )
    return dict(row)


@router.put("/users/{uid}")
async def update_user(
        uid: int,
        body: UserUpdateReq,
        pool: DatabasePool = Depends(get_db_pool),
        user: dict = Depends(require_login),
) -> dict:
    if "admin" not in (user.get("roles") or []):
        raise BizError(code=403, message="admin required")
    row = await pool.fetchrow("SELECT * FROM sys_users WHERE id = $1", uid)
    if row is None:
        raise BizError(code=404, message="user not found")
    now = int(datetime.now(timezone.utc).timestamp())
    updates: list[str] = []
    vals: list[Any] = []
    data = body.model_dump(exclude_unset=True)
    for key, val in data.items():
        updates.append(f"{key} = ${len(vals) + 1}")
        vals.append(val)
    updates.append(f"updated_at = ${len(vals) + 1}")
    vals.append(now)
    vals.append(uid)
    updated = await pool.fetchrow(
        f"UPDATE sys_users SET {', '.join(updates)} WHERE id = ${len(vals)} RETURNING id, username, display_name, roles, enabled, protected",
        *vals,
    )
    return dict(updated)


@router.delete("/users/{uid}")
async def delete_user(uid: int, pool: DatabasePool = Depends(get_db_pool), user: dict = Depends(require_login)) -> dict:
    if "admin" not in (user.get("roles") or []):
        raise BizError(code=403, message="admin required")
    row = await pool.fetchrow("SELECT protected FROM sys_users WHERE id = $1", uid)
    if row is None:
        raise BizError(code=404, message="user not found")
    if row["protected"]:
        raise BizError(code=403, message="protected user cannot be deleted")
    await pool.execute("DELETE FROM sys_users WHERE id = $1", uid)
    return {"ok": True}


@router.put("/users/{uid}/password")
async def change_password(
        uid: int,
        body: PasswordChangeReq,
        pool: DatabasePool = Depends(get_db_pool),
        user: dict = Depends(require_login),
) -> dict:
    row = await pool.fetchrow("SELECT password_hash FROM sys_users WHERE id = $1", uid)
    if row is None:
        raise BizError(code=404, message="user not found")
    caller = int(user.get("sub", 0))
    caller_roles = user.get("roles") or []
    if caller != uid and "admin" not in caller_roles:
        raise BizError(code=403, message="cannot change other user's password")
    if caller == uid and not verify_password(body.old_password, row["password_hash"]):
        raise BizError(code=1001, message="old password mismatch")
    await pool.execute(
        "UPDATE sys_users SET password_hash = $1, updated_at = $2 WHERE id = $3",
        hash_password(body.new_password),
        int(datetime.now(timezone.utc).timestamp()),
        uid,
    )
    return {"ok": True}


@router.put("/users/{uid}/enabled")
async def toggle_enabled(uid: int, body: dict, pool: DatabasePool = Depends(get_db_pool),
                         user: dict = Depends(require_login)) -> dict:
    if "admin" not in (user.get("roles") or []):
        raise BizError(code=403, message="admin required")
    enabled = bool(body.get("enabled", True))
    row = await pool.fetchrow(
        "UPDATE sys_users SET enabled = $1, updated_at = $2 WHERE id = $3 RETURNING id, username, enabled",
        enabled,
        int(datetime.now(timezone.utc).timestamp()),
        uid,
    )
    if row is None:
        raise BizError(code=404, message="user not found")
    return dict(row)


@router.put("/users/{uid}/roles")
async def update_roles(uid: int, body: RolesUpdateReq, pool: DatabasePool = Depends(get_db_pool),
                       user: dict = Depends(require_login)) -> dict:
    if "admin" not in (user.get("roles") or []):
        raise BizError(code=403, message="admin required")
    for rc in body.roles:
        r = await pool.fetchrow("SELECT 1 FROM sys_roles WHERE role_code = $1", rc)
        if r is None:
            raise BizError(code=1003, message=f"role not found: {rc}")
    row = await pool.fetchrow(
        "UPDATE sys_users SET roles = $1, updated_at = $2 WHERE id = $3 RETURNING id, username, roles",
        body.roles,
        int(datetime.now(timezone.utc).timestamp()),
        uid,
    )
    if row is None:
        raise BizError(code=404, message="user not found")
    return dict(row)


@router.get("/roles")
async def list_roles(pool: DatabasePool = Depends(get_db_pool), user: dict = Depends(require_login)) -> dict:
    rows = await pool.fetch("SELECT * FROM sys_roles ORDER BY id")
    return {"items": [dict(r) for r in rows], "total": len(rows)}


@router.post("/roles")
async def create_role(body: RoleCreateReq, pool: DatabasePool = Depends(get_db_pool),
                      user: dict = Depends(require_login)) -> dict:
    if "admin" not in (user.get("roles") or []):
        raise BizError(code=403, message="admin required")
    exist = await pool.fetchrow("SELECT 1 FROM sys_roles WHERE role_code = $1", body.role_code)
    if exist:
        raise BizError(code=409, message="role_code already exists")
    now = int(datetime.now(timezone.utc).timestamp())
    row = await pool.fetchrow(
        "INSERT INTO sys_roles (role_code, role_name, description, created_at, updated_at) "
        "VALUES ($1, $2, $3, $4, $4) RETURNING *",
        body.role_code, body.role_name, body.description, now,
    )
    return dict(row)


@router.put("/roles/{role_id}")
async def update_role(role_id: int, body: RoleUpdateReq, pool: DatabasePool = Depends(get_db_pool),
                      user: dict = Depends(require_login)) -> dict:
    if "admin" not in (user.get("roles") or []):
        raise BizError(code=403, message="admin required")
    row = await pool.fetchrow("SELECT * FROM sys_roles WHERE id = $1", role_id)
    if row is None:
        raise BizError(code=404, message="role not found")
    now = int(datetime.now(timezone.utc).timestamp())
    updates: list[str] = []
    vals: list[Any] = []
    for k, v in body.model_dump(exclude_unset=True).items():
        updates.append(f"{k} = ${len(vals) + 1}")
        vals.append(v)
    updates.append(f"updated_at = ${len(vals) + 1}")
    vals.append(now)
    vals.append(role_id)
    updated = await pool.fetchrow(
        f"UPDATE sys_roles SET {', '.join(updates)} WHERE id = ${len(vals)} RETURNING *",
        *vals,
    )
    return dict(updated)


@router.delete("/roles/{role_id}")
async def delete_role(role_id: int, pool: DatabasePool = Depends(get_db_pool),
                      user: dict = Depends(require_login)) -> dict:
    if "admin" not in (user.get("roles") or []):
        raise BizError(code=403, message="admin required")
    row = await pool.fetchrow("SELECT role_code FROM sys_roles WHERE id = $1", role_id)
    if row is None:
        raise BizError(code=404, message="role not found")
    if row["role_code"] == "admin":
        raise BizError(code=403, message="admin role cannot be deleted")
    await pool.execute("DELETE FROM sys_roles WHERE id = $1", role_id)
    return {"ok": True}
