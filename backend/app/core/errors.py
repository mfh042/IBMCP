from __future__ import annotations

import logging
from typing import Any

from fastapi import Request
from fastapi.exceptions import RequestValidationError
from fastapi.responses import JSONResponse
from starlette.exceptions import HTTPException as StarletteHTTPException

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


class ResultEnvelopeMiddleware:
    """统一响应包膜 + 全量错误兜底。

    正常响应（HTTP 2xx）被原样返回，仅错误路径走 {code, message, data} 包膜。
    """

    async def __call__(self, request: Request, call_next):
        try:
            response = await call_next(request)
            return response
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
        except Exception as e:
            logger.exception("unhandled exception at %s %s", request.method, request.url.path)
            return JSONResponse(
                status_code=200,
                content=Result(code=500, message="internal error", data=str(e)).model_dump(),
            )
