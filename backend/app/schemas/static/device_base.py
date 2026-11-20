from __future__ import annotations

from pydantic import BaseModel, ConfigDict, Field


class DataItemRef(BaseModel):
    model_config = ConfigDict(str_strip_whitespace=True)
    data_item_code: str
    address_rule: str | None = None
    collection_cycle_ms: int | None = Field(default=None, ge=100)


class BaseDeviceCreate(BaseModel):
    model_config = ConfigDict(str_strip_whitespace=True, extra="forbid")

    device_code: str = Field(min_length=1, max_length=64, pattern=r"^[A-Za-z0-9_-]+$")
    device_name: str = Field(min_length=1, max_length=128)
    device_category_code: str | None = Field(default=None, max_length=64)
    brand_code: str | None = Field(default=None, max_length=64)
    model_code: str | None = Field(default=None, max_length=64)
    serial_number: str | None = Field(default=None, max_length=128)
    manufacturer: str | None = Field(default=None, max_length=128)
    supported_protocols: list[str] = Field(default_factory=list)
    supported_ports: list[str] = Field(default_factory=list)
    data_items: list[DataItemRef] = Field(default_factory=list)
    description: str | None = None


class BaseDeviceUpdate(BaseModel):
    model_config = ConfigDict(str_strip_whitespace=True, extra="forbid")
    device_name: str | None = Field(default=None, min_length=1, max_length=128)
    device_category_code: str | None = None
    brand_code: str | None = None
    model_code: str | None = None
    serial_number: str | None = None
    manufacturer: str | None = None
    supported_protocols: list[str] | None = None
    supported_ports: list[str] | None = None
    data_items: list[DataItemRef] | None = None
    description: str | None = None


class BaseDeviceOut(BaseModel):
    id: int
    device_code: str
    device_name: str
    device_category_code: str | None
    brand_code: str | None
    model_code: str | None
    serial_number: str | None
    manufacturer: str | None
    supported_protocols: list[str]
    supported_ports: list[str]
    data_items: list[DataItemRef]
    description: str | None
    created_at: int
    updated_at: int
