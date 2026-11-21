# openEuler 本地 ISO 源实战指南（已废弃）

> ⚠️ **本方案已废弃，不再使用。**
>
> 自 2026-11-20 起，欧拉侧软件安装**不再使用本地 ISO 源**。当前统一口径：
>
> - **裸机装包**（nginx / gcc / cmake 等）：直接使用 openEuler 系统**在线源**，由 `deployments/openeuler/bootstrap.sh` 自动完成，无需手动配置任何本地源。
> - **Docker CE 源**：`bootstrap.sh` 写入清华镜像的 `docker-ce.repo`（可用 `DOCKER_REPO_MIRROR` 环境变量覆盖为内网镜像）。
> - **Python 依赖**：用系统自带 python3.11 建 venv + pip，`PIP_INDEX` 默认指向清华 PyPI 镜像（可覆盖为内网镜像）。
> - **PG15 / Redis / Node-RED**：走 Docker Compose，不裸机安装。
>
> 若未来遇到**完全离线、无任何镜像源**的部署场景，需另行评估离线方案（如搭建内网 yum 镜像仓库），不再使用挂载 ISO 到本地硬盘的方式。
>
> 本文档保留仅作历史参考，**请勿按其中步骤操作**。
>
> 当前环境搭建以 [09-openEuler环境准备实操指南](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/09-openEuler环境准备实操指南.md) 为准，代码结构见 [12-文件代码结构及代码说明](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/12-文件代码结构及代码说明.md)。