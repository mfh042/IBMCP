from __future__ import annotations

import json
import logging
from typing import Any

from fastapi import Request
from fastapi.exceptions import RequestValidationError
from fastapi.responses import JSONResponse, Response
from starlette.exceptions import HTTPException as StarletteHTTPException
from starlette.middleware.base import BaseHTTPMiddleware

from app.core.collector.protocol import CollectorError, to_api_error
from app.schemas.common import Result

logger = logging.getLogger(__name__)


class BizError(Exception):
    """业务层显式抛出的可控错误。

    code 为业务码（≠ HTTP status，HTTP 永远 200）。
    """

    def __init__(self, code: int, message: str, data: Any = None) -> None:
        self.code = code
        self.message = message
        self.data = data


class ResultEnvelopeMiddleware(BaseHTTPMiddleware):
    """统一响应包膜 + 全量错误兜底。

    成功响应统一包成 `{code: 0, message: "ok", data: ...}`（与 03 号契约一致）；
    OpenAPI / Swagger / ReDoc 等非业务 JSON 不包膜。
    """

    _SKIP_SUFFIXES = ("/openapi.json", "/docs", "/redoc")

    async def dispatch(self, request: Request, call_next):
        try:
            response = await call_next(request)
            return await self._wrap(request, response)
        except BizError as e:
            return JSONResponse(
                status_code=200,
                content=Result(code=e.code, message=e.message, data=e.data).model_dump(),
            )
        except CollectorError as e:
            code, message, extra = to_api_error(e)
            return JSONResponse(
                status_code=200,
                content=Result(code=code, message=message, data=extra or None).model_dump(),
            )
        except RequestValidationError as e:
            errors = [
                {"field": ".".join(str(p) for p in err["loc"]), "message": err["msg"]}
                for err in e.errors()
            ]
            return JSONResponse(
                status_code=200,
                content=Result(code=422, message="validation failed", data=errors).model_dump(),
            )
        except StarletteHTTPException as e:
            return JSONResponse(
                status_code=200,
                content=Result(code=e.status_code, message=str(e.detail), data=None).model_dump(),
            )
        except Exception:
            logger.exception("unhandled exception at %s %s", request.method, request.url.path)
            return JSONResponse(
                status_code=200,
                content=Result(code=500, message="internal error", data=None).model_dump(),
            )

    async def _wrap(self, request: Request, response):
        path = request.url.path
        if any(path.endswith(s) for s in self._SKIP_SUFFIXES):
            return response
        if response.status_code >= 400:
            return response
        content_type = response.headers.get("content-type", "")
        if not content_type.startswith("application/json"):
            return response
        if getattr(response, "body_iterator", None) is None:
            return response

        body = b"".join([chunk async for chunk in response.body_iterator])
        try:
            payload = json.loads(body) if body else None
        except json.JSONDecodeError:
            return Response(
                content=body,
                status_code=response.status_code,
                headers={"content-type": content_type},
            )

        if isinstance(payload, dict) and "code" in payload and "data" in payload:
            return JSONResponse(status_code=response.status_code, content=payload)

        return JSONResponse(
            status_code=response.status_code,
            content=Result(code=0, message="ok", data=payload).model_dump(),
        )
