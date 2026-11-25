-- 0003 control_commands.ext_command_id
-- 扩展是控制命令的唯一发号方，需要保存 write_point() 返回的 command_id，
-- 才能把 write_result 事件回写到台账。

ALTER TABLE control_commands ADD COLUMN IF NOT EXISTS ext_command_id BIGINT;
CREATE INDEX IF NOT EXISTS idx_commands_ext_id ON control_commands (ext_command_id);