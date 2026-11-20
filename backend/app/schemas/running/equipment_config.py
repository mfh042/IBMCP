from __future__ import annotations

from typing import Any

from pydantic import BaseModel, ConfigDict, Field


class RunningConfigCreate(BaseModel):
    model_config = ConfigDict(str_strip_whitespace=True, extra="forbid")

    equipment_code: str = Field(min_length=1, max_length=64)
    config_code: str = Field(min_length=1, max_length=64)
    config_name: str = Field(min_length=1, max_length=128)
    description: str | None = None


class RunningConfigUpdate(BaseModel):
    model_config = ConfigDict(str_strip_whitespace=True, extra="forbid")
    config_name: str | None = Field(default=None, min_length=1, max_length=128)
    description: str | None = None
    config_status: int | None = Field(default=None, ge=0, le=2)


class RunningConfigOut(BaseModel):
    id: int
    equipment_code: str
    config_code: str
    config_name: str
    config_status: int
    run_status: int
    current_revision: int
    description: str | None
    created_at: int
    updated_at: int


class PlanCompilerOut(BaseModel):
    revision: int
    flush_interval_ms: int
    device_count: int
    point_count: int
    plan: dict[str, Any]
