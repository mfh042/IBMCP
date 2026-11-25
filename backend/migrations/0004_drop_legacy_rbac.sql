-- 0004 drop legacy rbac tables users / roles / user_roles
-- 0001 曾创建旧版 RBAC 表 users / roles / user_roles（用户-角色多对多），
-- 0002 重构为 sys_users / sys_roles（roles 拍平为 TEXT[]），旧表再无任何代码引用。
-- 本迁移清理已部署环境中遗留的空表，保证全库与代码实际使用的表一致。

DROP TABLE IF EXISTS user_roles CASCADE;
DROP TABLE IF EXISTS roles CASCADE;
DROP TABLE IF EXISTS users CASCADE;