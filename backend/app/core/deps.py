from fastapi import Depends, HTTPException, Request, status
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer

from app.core.collector.ffi import get_collector
from app.core.collector.protocol import CollectorApi
from app.core.config import settings
from app.db.pool import DatabasePool

bearer = HTTPBearer(auto_error=False)


def get_db_pool() -> DatabasePool:
    from app.main import db_pool
    return db_pool


def get_collector_backend() -> CollectorApi:
    return get_collector(settings.collector_backend)


async def get_current_user(
        request: Request,
        auth: HTTPAuthorizationCredentials = Depends(bearer),
) -> dict | None:
    if auth is None:
        return None
    from app.core.security import decode_token
    payload = decode_token(auth.credentials)
    if payload is None:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="invalid token")
    request.state.user = payload
    return payload


async def require_login(
        user: dict | None = Depends(get_current_user),
) -> dict:
    if user is None:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="login required")
    return user
