import pytest

from app.services.plan_compiler import PlanCompiler, normalize_protocol


def row(**overrides):
    base = {
        "config_id": 1,
        "config_code": "C1",
        "equipment_code": "EQ1",
        "equipment_name": "设备1",
        "route_code": "R1",
        "route_config_id": 10,
        "binding_id": 100,
        "step_code": "S1",
        "device_code": "DEV1",
        "device_name": "PLC1",
        "protocol_code": "MODBUS_TCP",
        "device_connection": '{"host": "10.0.0.1", "port": 502}',
        "connect_timeout_ms": 3000,
        "read_timeout_ms": 1000,
        "collection_id": 1000,
        "item_code": "TEMP",
        "item_name": "温度",
        "address": "40001",
        "cycle_ms": 2000,
        "data_type": "float",
        "read_write": "R",
        "scale": 0.1,
        "offset": 0.0,
        "alarm_high": 80.0,
        "alarm_low": 5.0,
        "alarm_enabled": True,
        "unit": "℃",
    }
    base.update(overrides)
    return base


def build(rows):
    return PlanCompiler.__new__(PlanCompiler)._build_plan(rows, revision=7)


def test_normalize_protocol():
    assert normalize_protocol("MODBUS_TCP") == "modbustcp"
    assert normalize_protocol("Modbus TCP") == "modbustcp"
    assert normalize_protocol("opc-ua") == "opcua"


def test_build_plan_shape():
    plan = build([row()])
    assert plan["revision"] == 7
    assert plan["flush_interval_ms"] == 200
    dev = plan["devices"][0]
    assert dev["device_key"] == "DEV1"
    assert dev["protocol"] == "modbus_tcp"
    assert dev["connection"] == {"host": "10.0.0.1", "port": 502}
    point = dev["points"][0]
    assert point["point_id"] == 1000
    assert point["point_key"] == "DEV1:1000"
    assert point["scale"] == pytest.approx(0.1)
    assert point["alarm_enabled"] is True
    assert plan["plan_hash"]


def test_same_device_merges_points_and_keeps_shortest_cycle():
    plan = build([
        row(collection_id=1000, address="40001", cycle_ms=2000),
        row(collection_id=1001, address="40002", cycle_ms=5000),
        row(collection_id=1002, address="40001", cycle_ms=1000),
    ])
    assert len(plan["devices"]) == 1
    points = plan["devices"][0]["points"]
    assert len(points) == 2
    first = next(p for p in points if p["address"] == "40001")
    assert first["cycle_ms"] == 1000


def test_empty_address_rejected():
    with pytest.raises(ValueError, match="empty address"):
        build([row(address="  ")])


def test_zero_scale_rejected():
    with pytest.raises(ValueError, match="scale"):
        build([row(scale=0.0)])
