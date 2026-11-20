"""initial schema - all base/running/using/system tables

Revision ID: 0001
Revises:
Create Date: 2026-11-19

Aligned with: docs/02-数据模型与字段契约（FastAPI版）.md
"""

import sqlalchemy as sa
from alembic import op

revision = "0001"
down_revision = None
branch_labels = None
depends_on = None


def upgrade() -> None:
    conn = op.get_bind()

    # ============================================================
    # A 类：静态字典表 (10 张)
    # ============================================================

    conn.execute(sa.text("""
                         CREATE TABLE IF NOT EXISTS base_device_category_dict
                         (
                             id
                             BIGSERIAL
                             PRIMARY
                             KEY,
                             category_code
                             VARCHAR
                         (
                             64
                         ) NOT NULL UNIQUE,
                             category_name VARCHAR
                         (
                             128
                         ) NOT NULL,
                             status SMALLINT NOT NULL DEFAULT 1,
                             description TEXT,
                             created_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT,
                             updated_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT
                             );
                         """))

    conn.execute(sa.text("""
                         CREATE TABLE IF NOT EXISTS base_device_brand_model_dict
                         (
                             id
                             BIGSERIAL
                             PRIMARY
                             KEY,
                             brand_code
                             VARCHAR
                         (
                             64
                         ) NOT NULL,
                             brand_name VARCHAR
                         (
                             128
                         ) NOT NULL,
                             model_code VARCHAR
                         (
                             64
                         ) NOT NULL,
                             model_name VARCHAR
                         (
                             128
                         ) NOT NULL,
                             category_code VARCHAR
                         (
                             64
                         ),
                             status SMALLINT NOT NULL DEFAULT 1,
                             description TEXT,
                             created_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT,
                             updated_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT,
                             UNIQUE
                         (
                             brand_code,
                             model_code
                         )
                             );
                         """))

    conn.execute(sa.text("""
                         CREATE TABLE IF NOT EXISTS base_device_port_dict
                         (
                             id
                             BIGSERIAL
                             PRIMARY
                             KEY,
                             port_code
                             VARCHAR
                         (
                             64
                         ) NOT NULL UNIQUE,
                             port_name VARCHAR
                         (
                             128
                         ) NOT NULL,
                             port_type VARCHAR
                         (
                             32
                         ) NOT NULL,
                             default_baud INT,
                             status SMALLINT NOT NULL DEFAULT 1,
                             description TEXT,
                             created_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT,
                             updated_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT
                             );
                         """))

    conn.execute(sa.text("""
                         CREATE TABLE IF NOT EXISTS base_device_protocol_dict
                         (
                             id
                             BIGSERIAL
                             PRIMARY
                             KEY,
                             protocol_code
                             VARCHAR
                         (
                             64
                         ) NOT NULL UNIQUE,
                             protocol_name VARCHAR
                         (
                             128
                         ) NOT NULL,
                             protocol_category VARCHAR
                         (
                             32
                         ),
                             has_driver BOOLEAN NOT NULL DEFAULT TRUE,
                             status SMALLINT NOT NULL DEFAULT 1,
                             description TEXT,
                             created_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT,
                             updated_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT
                             );
                         """))

    conn.execute(sa.text("""
                         CREATE TABLE IF NOT EXISTS base_device_base_info
                         (
                             id
                             BIGSERIAL
                             PRIMARY
                             KEY,
                             device_code
                             VARCHAR
                         (
                             64
                         ) NOT NULL UNIQUE,
                             device_name VARCHAR
                         (
                             128
                         ) NOT NULL,
                             category_code VARCHAR
                         (
                             64
                         ),
                             brand_code VARCHAR
                         (
                             64
                         ),
                             model_code VARCHAR
                         (
                             64
                         ),
                             port_code VARCHAR
                         (
                             64
                         ),
                             supported_protocols VARCHAR
                         (
                             64
                         ),
                             device_role VARCHAR
                         (
                             32
                         ),
                             location VARCHAR
                         (
                             256
                         ),
                             description TEXT,
                             created_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT,
                             updated_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT
                             );
                         """))

    conn.execute(sa.text("""
                         CREATE TABLE IF NOT EXISTS base_device_data_item_dict
                         (
                             id
                             BIGSERIAL
                             PRIMARY
                             KEY,
                             item_code
                             VARCHAR
                         (
                             64
                         ) NOT NULL UNIQUE,
                             item_name VARCHAR
                         (
                             128
                         ) NOT NULL,
                             category_code VARCHAR
                         (
                             64
                         ),
                             protocol_code VARCHAR
                         (
                             64
                         ),
                             data_type VARCHAR
                         (
                             32
                         ) NOT NULL DEFAULT 'float',
                             read_write VARCHAR
                         (
                             4
                         ) NOT NULL DEFAULT 'R',
                             unit VARCHAR
                         (
                             32
                         ),
                             precision INT,
                             min_value DECIMAL
                         (
                             18,
                             4
                         ),
                             max_value DECIMAL
                         (
                             18,
                             4
                         ),
                             address_rule VARCHAR
                         (
                             128
                         ),
                             collection_cycle INT NOT NULL DEFAULT 1,
                             scale DOUBLE PRECISION NOT NULL DEFAULT 1.0 CHECK
                         (
                             scale
                             <>
                             0
                         ),
                             "offset" DOUBLE PRECISION NOT NULL DEFAULT 0.0,
                             alarm_high DOUBLE PRECISION NOT NULL DEFAULT 0.0,
                             alarm_low DOUBLE PRECISION NOT NULL DEFAULT 0.0,
                             alarm_enabled BOOLEAN NOT NULL DEFAULT FALSE,
                             description TEXT,
                             created_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT,
                             updated_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT
                             );
                         """))

    conn.execute(sa.text("""
                         CREATE TABLE IF NOT EXISTS base_process_route_resource
                         (
                             id
                             BIGSERIAL
                             PRIMARY
                             KEY,
                             route_code
                             VARCHAR
                         (
                             64
                         ) NOT NULL UNIQUE,
                             route_name VARCHAR
                         (
                             128
                         ) NOT NULL,
                             resource_type VARCHAR
                         (
                             32
                         ),
                             resource_url VARCHAR
                         (
                             512
                         ),
                             thumbnail_url VARCHAR
                         (
                             512
                         ),
                             version VARCHAR
                         (
                             32
                         ),
                             status SMALLINT NOT NULL DEFAULT 1,
                             description TEXT,
                             created_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT,
                             updated_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT
                             );
                         """))

    conn.execute(sa.text("""
                         CREATE TABLE IF NOT EXISTS base_process_route_step
                         (
                             id
                             BIGSERIAL
                             PRIMARY
                             KEY,
                             route_code
                             VARCHAR
                         (
                             64
                         ) NOT NULL REFERENCES base_process_route_resource
                         (
                             route_code
                         ) ON UPDATE CASCADE,
                             step_code VARCHAR
                         (
                             64
                         ) NOT NULL,
                             step_seq INT NOT NULL,
                             step_name VARCHAR
                         (
                             128
                         ) NOT NULL,
                             device_code VARCHAR
                         (
                             64
                         ) REFERENCES base_device_base_info
                         (
                             device_code
                         ) ON UPDATE CASCADE ON DELETE SET NULL,
                             duration_time INT,
                             work_center VARCHAR
                         (
                             128
                         ),
                             step_params JSONB NOT NULL DEFAULT '{}',
                             prev_step_code VARCHAR
                         (
                             64
                         ),
                             is_critical SMALLINT NOT NULL DEFAULT 0,
                             status SMALLINT NOT NULL DEFAULT 1,
                             created_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT,
                             updated_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT,
                             UNIQUE
                         (
                             route_code,
                             step_code
                         )
                             );
                         """))

    conn.execute(sa.text("""
                         CREATE TABLE IF NOT EXISTS base_equipment_base_info
                         (
                             id
                             BIGSERIAL
                             PRIMARY
                             KEY,
                             equipment_code
                             VARCHAR
                         (
                             64
                         ) NOT NULL UNIQUE,
                             equipment_name VARCHAR
                         (
                             128
                         ) NOT NULL,
                             customer_code VARCHAR
                         (
                             64
                         ),
                             customer_name VARCHAR
                         (
                             128
                         ),
                             category_code VARCHAR
                         (
                             64
                         ),
                             brand_code VARCHAR
                         (
                             64
                         ),
                             model_code VARCHAR
                         (
                             64
                         ),
                             location VARCHAR
                         (
                             256
                         ),
                             description TEXT,
                             created_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT,
                             updated_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT
                             );
                         """))

    conn.execute(sa.text("""
                         CREATE TABLE IF NOT EXISTS base_equipment_function_dict
                         (
                             id
                             BIGSERIAL
                             PRIMARY
                             KEY,
                             function_code
                             VARCHAR
                         (
                             64
                         ) NOT NULL UNIQUE,
                             function_name VARCHAR
                         (
                             128
                         ) NOT NULL,
                             function_type VARCHAR
                         (
                             32
                         ) NOT NULL,
                             category_code VARCHAR
                         (
                             64
                         ),
                             equipment_code VARCHAR
                         (
                             64
                         ),
                             status SMALLINT NOT NULL DEFAULT 1,
                             description TEXT,
                             created_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT,
                             updated_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT
                             );
                         """))

    # ============================================================
    # B 类：运行动态配置表 (5 张)
    # ============================================================

    conn.execute(sa.text("""
                         CREATE TABLE IF NOT EXISTS running_equipment_config
                         (
                             id
                             BIGSERIAL
                             PRIMARY
                             KEY,
                             config_code
                             VARCHAR
                         (
                             64
                         ) NOT NULL UNIQUE,
                             equipment_code VARCHAR
                         (
                             64
                         ) NOT NULL REFERENCES base_equipment_base_info
                         (
                             equipment_code
                         ) ON UPDATE CASCADE,
                             equipment_name VARCHAR
                         (
                             128
                         ),
                             customer_code VARCHAR
                         (
                             64
                         ),
                             customer_name VARCHAR
                         (
                             128
                         ),
                             category_code VARCHAR
                         (
                             64
                         ),
                             location VARCHAR
                         (
                             256
                         ),
                             config_status SMALLINT NOT NULL DEFAULT 0,
                             run_status SMALLINT NOT NULL DEFAULT 0,
                             remark TEXT,
                             created_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT,
                             updated_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT
                             );
                         """))

    conn.execute(sa.text("""
                         CREATE TABLE IF NOT EXISTS running_equipment_route
                         (
                             id
                             BIGSERIAL
                             PRIMARY
                             KEY,
                             config_id
                             BIGINT
                             NOT
                             NULL
                             REFERENCES
                             running_equipment_config
                         (
                             id
                         ) ON DELETE CASCADE,
                             route_code VARCHAR
                         (
                             64
                         ) NOT NULL REFERENCES base_process_route_resource
                         (
                             route_code
                         ) ON UPDATE CASCADE,
                             route_name VARCHAR
                         (
                             128
                         ),
                             route_seq INT NOT NULL DEFAULT 10,
                             route_version VARCHAR
                         (
                             32
                         ),
                             route_params JSONB NOT NULL DEFAULT '{}',
                             enabled SMALLINT NOT NULL DEFAULT 1,
                             created_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT,
                             updated_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT,
                             UNIQUE
                         (
                             config_id,
                             route_code
                         )
                             );
                         """))

    conn.execute(sa.text("""
                         CREATE TABLE IF NOT EXISTS running_equipment_function
                         (
                             id
                             BIGSERIAL
                             PRIMARY
                             KEY,
                             config_id
                             BIGINT
                             NOT
                             NULL
                             REFERENCES
                             running_equipment_config
                         (
                             id
                         ) ON DELETE CASCADE,
                             function_code VARCHAR
                         (
                             64
                         ) NOT NULL REFERENCES base_equipment_function_dict
                         (
                             function_code
                         ) ON UPDATE CASCADE,
                             function_name VARCHAR
                         (
                             128
                         ),
                             function_type VARCHAR
                         (
                             32
                         ),
                             enabled SMALLINT NOT NULL DEFAULT 1,
                             function_params JSONB NOT NULL DEFAULT '{}',
                             remark TEXT,
                             created_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT,
                             updated_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT,
                             UNIQUE
                         (
                             config_id,
                             function_code
                         )
                             );
                         """))

    conn.execute(sa.text("""
                         CREATE TABLE IF NOT EXISTS running_route_device
                         (
                             id
                             BIGSERIAL
                             PRIMARY
                             KEY,
                             route_config_id
                             BIGINT
                             NOT
                             NULL
                             REFERENCES
                             running_equipment_route
                         (
                             id
                         ) ON DELETE CASCADE,
                             route_code VARCHAR
                         (
                             64
                         ) NOT NULL,
                             step_code VARCHAR
                         (
                             64
                         ) NOT NULL,
                             step_name VARCHAR
                         (
                             128
                         ),
                             step_seq INT,
                             device_code VARCHAR
                         (
                             64
                         ) NOT NULL REFERENCES base_device_base_info
                         (
                             device_code
                         ) ON UPDATE CASCADE ON DELETE CASCADE,
                             device_name VARCHAR
                         (
                             128
                         ),
                             device_connection JSONB NOT NULL DEFAULT '{}',
                             port_code VARCHAR
                         (
                             64
                         ),
                             protocol_code VARCHAR
                         (
                             64
                         ) NOT NULL DEFAULT 'MODBUS_TCP',
                             enabled SMALLINT NOT NULL DEFAULT 1,
                             connect_timeout_ms INT NOT NULL DEFAULT 3000,
                             read_timeout_ms INT NOT NULL DEFAULT 1000,
                             created_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT,
                             updated_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT,
                             UNIQUE
                         (
                             route_config_id,
                             step_code,
                             device_code
                         )
                             );
                         """))

    conn.execute(sa.text("""
                         CREATE TABLE IF NOT EXISTS running_device_collection
                         (
                             id
                             BIGSERIAL
                             PRIMARY
                             KEY,
                             device_binding_id
                             BIGINT
                             NOT
                             NULL
                             REFERENCES
                             running_route_device
                         (
                             id
                         ) ON DELETE CASCADE,
                             device_code VARCHAR
                         (
                             64
                         ) NOT NULL,
                             item_code VARCHAR
                         (
                             64
                         ) NOT NULL REFERENCES base_device_data_item_dict
                         (
                             item_code
                         ) ON UPDATE CASCADE,
                             item_name VARCHAR
                         (
                             128
                         ),
                             collection_cycle INT,
                             address_override VARCHAR
                         (
                             128
                         ),
                             data_type VARCHAR
                         (
                             32
                         ),
                             read_write VARCHAR
                         (
                             4
                         ),
                             scale DOUBLE PRECISION,
                             "offset" DOUBLE PRECISION,
                             alarm_high DOUBLE PRECISION,
                             alarm_low DOUBLE PRECISION,
                             alarm_enabled BOOLEAN,
                             enabled SMALLINT NOT NULL DEFAULT 1,
                             created_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT,
                             updated_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT,
                             UNIQUE
                         (
                             device_binding_id,
                             item_code
                         )
                             );
                         """))

    # ============================================================
    # C 类：运行期结果与快照表
    # ============================================================

    conn.execute(sa.text("""
                         CREATE TABLE IF NOT EXISTS using_devices
                         (
                             id
                             BIGSERIAL
                             PRIMARY
                             KEY,
                             device_code
                             VARCHAR
                         (
                             64
                         ) NOT NULL UNIQUE,
                             device_name VARCHAR
                         (
                             128
                         ) NOT NULL,
                             equipment_code VARCHAR
                         (
                             64
                         ) NOT NULL DEFAULT '',
                             equipment_name VARCHAR
                         (
                             128
                         ) NOT NULL DEFAULT '',
                             protocol_code VARCHAR
                         (
                             64
                         ) NOT NULL DEFAULT 'MODBUS_TCP',
                             device_connection JSONB NOT NULL DEFAULT '{}',
                             status SMALLINT NOT NULL DEFAULT 0,
                             remark TEXT,
                             created_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT,
                             updated_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT
                             );
                         """))

    conn.execute(sa.text("""
                         CREATE TABLE IF NOT EXISTS using_points
                         (
                             id
                             BIGSERIAL
                             PRIMARY
                             KEY,
                             device_code
                             VARCHAR
                         (
                             64
                         ) NOT NULL REFERENCES using_devices
                         (
                             device_code
                         ) ON UPDATE CASCADE ON DELETE CASCADE,
                             using_point_id BIGINT,
                             item_name VARCHAR
                         (
                             128
                         ) NOT NULL,
                             address VARCHAR
                         (
                             128
                         ) NOT NULL,
                             data_type VARCHAR
                         (
                             32
                         ) NOT NULL DEFAULT 'float',
                             read_write VARCHAR
                         (
                             4
                         ) NOT NULL DEFAULT 'R',
                             unit VARCHAR
                         (
                             32
                         ) NOT NULL DEFAULT '',
                             scale DOUBLE PRECISION NOT NULL DEFAULT 1.0,
                             "offset" DOUBLE PRECISION NOT NULL DEFAULT 0.0,
                             collection_cycle INT NOT NULL DEFAULT 1,
                             cycle_ms INT NOT NULL DEFAULT 1000,
                             alarm_high DOUBLE PRECISION NOT NULL DEFAULT 0.0,
                             alarm_low DOUBLE PRECISION NOT NULL DEFAULT 0.0,
                             alarm_enabled BOOLEAN NOT NULL DEFAULT FALSE,
                             enabled BOOLEAN NOT NULL DEFAULT TRUE,
                             created_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT,
                             updated_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT,
                             UNIQUE
                         (
                             device_code,
                             item_name
                         ),
                             UNIQUE
                         (
                             device_code,
                             address
                         )
                             );
                         """))

    conn.execute(sa.text("""
                         CREATE TABLE IF NOT EXISTS time_series_data
                         (
                             ts
                             TIMESTAMPTZ
                             NOT
                             NULL,
                             point_id
                             BIGINT
                             NOT
                             NULL
                             REFERENCES
                             using_points
                         (
                             id
                         ) ON DELETE CASCADE,
                             value DOUBLE PRECISION NOT NULL,
                             quality VARCHAR
                         (
                             16
                         ) NOT NULL DEFAULT 'Good',
                             raw_value TEXT,
                             PRIMARY KEY
                         (
                             ts,
                             point_id
                         )
                             );
                         """))

    conn.execute(sa.text("""
                         CREATE TABLE IF NOT EXISTS using_alarms
                         (
                             id
                             BIGSERIAL
                             PRIMARY
                             KEY,
                             device_code
                             VARCHAR
                         (
                             64
                         ) NOT NULL REFERENCES using_devices
                         (
                             device_code
                         ) ON UPDATE CASCADE ON DELETE CASCADE,
                             point_address VARCHAR
                         (
                             128
                         ),
                             point_name VARCHAR
                         (
                             128
                         ),
                             level SMALLINT NOT NULL DEFAULT 1,
                             state SMALLINT NOT NULL DEFAULT 1,
                             message TEXT,
                             value DOUBLE PRECISION NOT NULL DEFAULT 0.0,
                             triggered_at BIGINT NOT NULL,
                             acknowledged_at BIGINT,
                             acknowledged_by VARCHAR
                         (
                             64
                         ),
                             recovered_at BIGINT
                             );
                         """))

    conn.execute(sa.text("""
                         CREATE TABLE IF NOT EXISTS control_commands
                         (
                             id
                             BIGSERIAL
                             PRIMARY
                             KEY,
                             device_id
                             BIGINT
                             NOT
                             NULL
                             REFERENCES
                             using_devices
                         (
                             id
                         ) ON DELETE CASCADE,
                             point_id BIGINT NOT NULL REFERENCES using_points
                         (
                             id
                         ) ON DELETE CASCADE,
                             point_name VARCHAR
                         (
                             128
                         ),
                             value TEXT NOT NULL,
                             status SMALLINT NOT NULL DEFAULT 0,
                             message TEXT,
                             issued_at BIGINT NOT NULL,
                             issued_by VARCHAR
                         (
                             64
                         ),
                             finished_at BIGINT
                             );
                         """))

    # ============================================================
    # D 类：RBAC 系统管理表
    # ============================================================

    conn.execute(sa.text("""
                         CREATE TABLE IF NOT EXISTS roles
                         (
                             id
                             BIGSERIAL
                             PRIMARY
                             KEY,
                             name
                             VARCHAR
                         (
                             64
                         ) NOT NULL UNIQUE,
                             description TEXT
                             );
                         """))

    conn.execute(sa.text("""
                         CREATE TABLE IF NOT EXISTS users
                         (
                             id
                             BIGSERIAL
                             PRIMARY
                             KEY,
                             username
                             VARCHAR
                         (
                             64
                         ) NOT NULL UNIQUE,
                             password_hash VARCHAR
                         (
                             255
                         ) NOT NULL,
                             display_name VARCHAR
                         (
                             128
                         ),
                             email VARCHAR
                         (
                             255
                         ),
                             phone VARCHAR
                         (
                             32
                         ),
                             enabled BOOLEAN NOT NULL DEFAULT TRUE,
                             last_login_at BIGINT,
                             created_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT,
                             updated_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT
                             );
                         """))

    conn.execute(sa.text("""
                         CREATE TABLE IF NOT EXISTS user_roles
                         (
                             user_id
                             BIGINT
                             NOT
                             NULL
                             REFERENCES
                             users
                         (
                             id
                         ) ON DELETE CASCADE,
                             role_id BIGINT NOT NULL REFERENCES roles
                         (
                             id
                         ) ON DELETE CASCADE,
                             PRIMARY KEY
                         (
                             user_id,
                             role_id
                         )
                             );
                         """))

    # ============================================================
    # E 类：重构新增表 (3 张)
    # ============================================================

    conn.execute(sa.text("""
                         CREATE TABLE IF NOT EXISTS runtime_plan_revision
                         (
                             id
                             BIGSERIAL
                             PRIMARY
                             KEY,
                             revision
                             BIGINT
                             NOT
                             NULL,
                             config_id
                             BIGINT
                             REFERENCES
                             running_equipment_config
                         (
                             id
                         ) ON DELETE SET NULL,
                             plan_hash VARCHAR
                         (
                             64
                         ) NOT NULL,
                             status SMALLINT NOT NULL DEFAULT 0,
                             extension_error TEXT,
                             created_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT,
                             applied_at BIGINT
                             );
                         """))

    conn.execute(sa.text("""
                         CREATE TABLE IF NOT EXISTS audit_config_change
                         (
                             id
                             BIGSERIAL
                             PRIMARY
                             KEY,
                             target_level
                             VARCHAR
                         (
                             16
                         ) NOT NULL,
                             target_id BIGINT NOT NULL,
                             action VARCHAR
                         (
                             16
                         ) NOT NULL,
                             plan_revision_before BIGINT,
                             plan_revision_after BIGINT,
                             operator VARCHAR
                         (
                             64
                         ),
                             detail JSONB NOT NULL DEFAULT '{}',
                             created_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT
                             );
                         """))

    conn.execute(sa.text("""
                         CREATE TABLE IF NOT EXISTS using_device_runtime
                         (
                             device_key
                             VARCHAR
                         (
                             96
                         ) PRIMARY KEY,
                             protocol_code VARCHAR
                         (
                             64
                         ),
                             status SMALLINT NOT NULL DEFAULT 0,
                             last_error TEXT,
                             points_ok INT NOT NULL DEFAULT 0,
                             points_fail INT NOT NULL DEFAULT 0,
                             snapshot_at BIGINT NOT NULL DEFAULT EXTRACT
                         (
                             EPOCH
                             FROM
                             NOW
                         (
                         ))::BIGINT
                             );
                         """))

    # ============================================================
    # 索引
    # ============================================================
    conn.execute(sa.text("CREATE INDEX IF NOT EXISTS idx_ts_data_point ON time_series_data (point_id);"))
    conn.execute(sa.text("CREATE INDEX IF NOT EXISTS idx_ts_data_ts ON time_series_data (ts DESC);"))
    conn.execute(sa.text("CREATE INDEX IF NOT EXISTS idx_alarms_device_state ON using_alarms (device_code, state);"))
    conn.execute(sa.text("CREATE INDEX IF NOT EXISTS idx_commands_status ON control_commands (status);"))
    conn.execute(sa.text("CREATE INDEX IF NOT EXISTS idx_plan_revision_hash ON runtime_plan_revision (plan_hash);"))
    conn.execute(sa.text("CREATE INDEX IF NOT EXISTS idx_plan_revision_status ON runtime_plan_revision (status);"))
    conn.execute(
        sa.text("CREATE INDEX IF NOT EXISTS idx_audit_target ON audit_config_change (target_level, target_id);"))
    conn.execute(sa.text("CREATE INDEX IF NOT EXISTS idx_audit_created ON audit_config_change (created_at DESC);"))
    conn.execute(sa.text("CREATE INDEX IF NOT EXISTS idx_using_devices_status ON using_devices (status);"))
    conn.execute(sa.text("CREATE INDEX IF NOT EXISTS idx_runtime_status ON using_device_runtime (status);"))


def downgrade() -> None:
    conn = op.get_bind()

    tables = [
        "audit_config_change",
        "runtime_plan_revision",
        "user_roles",
        "roles",
        "users",
        "control_commands",
        "using_alarms",
        "time_series_data",
        "using_points",
        "using_devices",
        "using_device_runtime",
        "running_device_collection",
        "running_route_device",
        "running_equipment_function",
        "running_equipment_route",
        "running_equipment_config",
        "base_equipment_function_dict",
        "base_equipment_base_info",
        "base_process_route_step",
        "base_process_route_resource",
        "base_device_data_item_dict",
        "base_device_base_info",
        "base_device_protocol_dict",
        "base_device_port_dict",
        "base_device_brand_model_dict",
        "base_device_category_dict",
    ]
    for t in reversed(tables):
        conn.execute(sa.text(f"DROP TABLE IF EXISTS {t} CASCADE;"))
