# deployments/ — 部署与物料版本管控总览

本目录存放 IBMCP 落地运行所需的全部部署脚本与配套物料，与 `packaging/`（运行时模板）配合使用。

## 子目录

| 子目录 | 定位 | 入口 |
|---|---|---|
| [`openeuler/`](./openeuler/README.md) | openEuler 24.03 LTS 上的**构建 + 部署**全流程编排（13 个脚本 / 配置） | `openeuler/README.md` |
| [`nodered/`](./nodered/README.md) | Node-RED **流文件 + 配置 + palette 版本**的版本管控 | `nodered/README.md` |

## 与 packaging/ 的分工

```
deployments/        ← 「怎么部署」：脚本编排、物料版本
  openeuler/          构建 / 部署 / 打包 / 回滚 / 离线还原（在欧拉侧执行）
  nodered/            Node-RED 流与配置版本管理

packaging/          ← 「部署什么」：运行期模板
  nginx/              Nginx 反代模板           → bootstrap.sh / deploy.sh 安装
  systemd/            后端 systemd unit 模板   → deploy.sh 安装
  docker/             后端容器化备选方案（非主路径，被 eSync 排除）
```

- `deployments/openeuler/` 是执行方：`bootstrap.sh` / `deploy.sh` 会 `sed` 替换 `packaging/nginx/ibmcp.conf` 与 `packaging/systemd/ibmcp-api.service` 里的路径占位符（`/home/meng/ibmcp`）后安装到系统，详见 `packaging/README.md`；
- `packaging/docker/` 是**非主路径**的备选方案，默认不走；eSync 同步源码到欧拉时排除该子目录；
- Windows 侧远程编排（`euler-remote.ps1` 等）也在 `openeuler/` 下，但含本地路径与密码，已被 `.gitignore` 排除（见 `openeuler/README.md`）。

## 快速导航

- 新机器从 0 部署：`openeuler/README.md` → 执行 `bootstrap.sh` → `build.sh` → `deploy.sh`；
- Node-RED 流变更：`nodered/README.md` → 编辑 `flows/` → `sync.sh` 推送到欧拉；
- 离线 / 灾备：`openeuler/make-bundle.sh`（打包）、`restore-bundle.sh`（还原）；
- 运行期模板细节：`../packaging/README.md`。

## 关联文档

- `docs/0-Drogon/05-部署与运行.md` — 部署全流程主文档；
- `docs/0-Drogon/10-环境变量与配置.md` — 环境变量与配置来源；
- `docs/0-Drogon/12-代码文档一致性核查清单.md` — 本目录与代码 / 文档的契约核查基准。