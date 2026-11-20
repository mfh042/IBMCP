from __future__ import annotations

from typing import Generic, TypeVar

from pydantic import BaseModel, ConfigDict, Field

T = TypeVar("T")


class Result(BaseModel, Generic[T]):
    """统一响应包膜，对齐 01 号文档 {code, message, data} 结构。"""
    model_config = ConfigDict(arbitrary_types_allowed=True)

    code: int = 0
    message: str = "ok"
    data: T | None = None


class PageParams(BaseModel):
    page: int = Field(default=1, ge=1)
    size: int = Field(default=20, ge=1, le=500)
    keyword: str | None = None


class PageResult(BaseModel, Generic[T]):
    items: list[T]
    total: int


class ErrorItem(BaseModel):
    field: str
    message: str
