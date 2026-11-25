-- 0005 add supported_ports / data_items columns to base_device_base_info
-- 前端设备主数据提交时携带 supported_ports / data_items 字段，补齐两列。
-- 存储形态沿用 supported_protocols 列约定：以文本存入，读写端自行序列化。

ALTER TABLE base_device_base_info
    ADD COLUMN IF NOT EXISTS supported_ports character varying,
    ADD COLUMN IF NOT EXISTS data_items text;