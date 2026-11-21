import asyncio

import pytest

from app.core.collector.stub import StubCollector
from app.services.command_waiter import CommandWaiter, command_key


@pytest.mark.asyncio
async def test_waiter_resolves_registered_future():
    waiter = CommandWaiter()
    key = command_key("DEV1", 10)
    fut = waiter.register(key)

    assert waiter.resolve(key, {"command_id": 1, "success": True}) is True
    assert await asyncio.wait_for(fut, timeout=0.1) == {"command_id": 1, "success": True}


def test_waiter_fifo_and_discard():
    async def scenario():
        waiter = CommandWaiter()
        key = command_key("DEV1", 10)
        first = waiter.register(key)
        second = waiter.register(key)

        waiter.discard(key, first)
        assert waiter.resolve(key, {"command_id": 2}) is True
        assert await asyncio.wait_for(second, timeout=0.1) == {"command_id": 2}

        assert waiter.resolve(key, {"command_id": 3}) is False

    asyncio.run(scenario())


def test_stub_emits_device_status_on_plan_and_shutdown():
    stub = StubCollector()
    stub.init()
    events = []
    stub.set_event_callback(events.append)

    rc = stub.apply_plan(
        {
            "devices": [{
                "device_key": "DEV1",
                "protocol": "modbus_tcp",
                "points": [{"point_id": 1, "address": "40001", "scale": 1.0}],
            }]
        },
        revision=1,
    )
    assert rc == 0
    assert "plan_ack" in [e["type"] for e in events]
    assert events[-1] == {
        "type": "device_status",
        "device_key": "DEV1",
        "status": 1,
        "reason": "connected",
        "ts_ms": events[-1]["ts_ms"],
    }

    stub.shutdown()
    assert events[-1]["type"] == "device_status"
    assert events[-1]["status"] == 0
