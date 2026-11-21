import pytest

from app.api.v1.static_dict import _decode_row, _validate_body
from app.core.errors import BizError

META = {
    "id": {
        "data_type": "bigint",
        "is_nullable": "NO",
        "character_maximum_length": None,
        "column_default": "nextval('x_id_seq'::regclass)",
    },
    "item_code": {
        "data_type": "character varying",
        "is_nullable": "NO",
        "character_maximum_length": 8,
        "column_default": None,
    },
    "scale": {
        "data_type": "double precision",
        "is_nullable": "NO",
        "character_maximum_length": None,
        "column_default": "1.0",
    },
    "alarm_enabled": {
        "data_type": "boolean",
        "is_nullable": "NO",
        "character_maximum_length": None,
        "column_default": "false",
    },
    "created_at": {
        "data_type": "bigint",
        "is_nullable": "NO",
        "character_maximum_length": None,
        "column_default": "0",
    },
}


def test_unknown_field_rejected():
    with pytest.raises(BizError) as exc:
        _validate_body("data_items", {"item_code": "T1", "nope": 1}, META, partial=False)
    assert exc.value.code == 422
    assert exc.value.data["fields"] == ["nope"]


def test_readonly_field_rejected():
    with pytest.raises(BizError) as exc:
        _validate_body("data_items", {"id": 1}, META, partial=False)
    assert exc.value.code == 422


def test_missing_required_field():
    with pytest.raises(BizError) as exc:
        _validate_body("data_items", {"scale": 1.0}, META, partial=False)
    assert exc.value.code == 422
    assert "item_code" in exc.value.data["fields"]


def test_max_length_enforced():
    with pytest.raises(BizError) as exc:
        _validate_body("data_items", {"item_code": "X" * 9}, META, partial=False)
    assert exc.value.code == 422
    assert exc.value.data["max_length"] == 8


def test_type_coercion():
    data = _validate_body(
        "data_items",
        {"item_code": "T1", "scale": "2.5", "alarm_enabled": "true"},
        META,
        partial=False,
    )
    assert data["scale"] == 2.5
    assert data["alarm_enabled"] is True


def test_decode_jsonb():
    meta = {"step_params": {"data_type": "jsonb"}}
    row = {"id": 1, "step_params": '{"a": 1}'}
    assert _decode_row(row, meta)["step_params"] == {"a": 1}
