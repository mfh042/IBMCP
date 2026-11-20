# openEuler 本地 ISO 源实战指南

> 结合虚拟机离线环境的踩坑排查过程整理，涵盖本地源搭建、常见报错排错、软件安装与源维护的完整说明，可作为日后搭建 Linux
> 离线环境的参考。

---

## 一、本地 ISO 源搭建

在虚拟机或离线环境中，使用光盘镜像（ISO）搭建本地源是最高效的方式。

### 1. 挂载光盘镜像

确保 ISO 镜像已连接到虚拟机光驱，然后挂载到系统目录：

```bash
mkdir -p /mnt/cdrom
mount /dev/cdrom /mnt/cdrom
```

### 2. 复制到本地硬盘（关键优化）

- **为什么要复制？** 虚拟光驱有时会出现不稳定的底层问题（如诡异的 `Bad file:// URL` 读取失败）。将光盘内容复制到硬盘后，读取速度极快且稳定可靠。

```bash
mkdir -p /data/everything
cp -r /mnt/cdrom/* /data/everything/
```

### 3. 创建本地源配置文件

用重定向方式生成一个干净、无隐藏字符的配置文件：

```bash
cat > /etc/yum.repos.d/local-data.repo << EOF
[local-data]
name=Local Everything Data
baseurl=file:///data/everything
enabled=1
gpgcheck=0
EOF
```

> 关键：`baseurl` 必须指向真正包含 `repodata` 文件夹的目录，不要多加子目录（如 `/Packages`）。

### 4. 刷新源缓存

```bash
sudo dnf clean all
sudo dnf makecache
```

---

## 二、核心踩坑与排错思路

本次配置过程中遇到的典型问题及解决方法：

### 1. 路径不对齐（`Couldn't open file`）

`Everything` 版 ISO 包体较大，复制到硬盘后目录结构可能与预期不同，导致找不到 `repodata`。

**排错方法**——用 `find` 精准定位索引目录：

```bash
find /data/ -type d -name repodata
```

根据输出结果，把 `baseurl` 修正到 `repodata` 所在的**上一级目录**。例如本例中真正的路径是 `/data/everything`，而不是
`/data/everything/Packages`。

### 2. 隐藏字符陷阱（`Bad file:// URL`）

在终端中多次复制粘贴长路径，极易混入中文标点或不可见空格，使系统无法识别路径。

**排错方法**——放弃手动编辑，改用 `cat > file << EOF` 一次性重写配置文件，确保文件绝对纯净，不带任何多余字符。

### 3. 文件名写错

配置文件实际名称（如 `local-data.repo`）与修改命令中的名称（如 `local-cdrom.repo`）不一致，导致 `sed`
找不到文件、修改无效。操作时务必核对文件名一致。

---

## 三、软件安装与管理

### 1. 指定本地源安装（屏蔽无效在线源）

当系统同时存在可用本地源和不可联网的在线源（如 `update`）时，安装会因尝试读取在线源而报错。此时应**强制只使用本地源**：

```bash
sudo dnf install openssh-server openssh-clients -y --disablerepo="update"
```

也可在配置阶段直接禁用在线源：

```bash
sudo yum-config-manager --disable update
```

### 2. 查询本地源包含的软件

安装前先确认软件包名称：

```bash
sudo dnf search <关键字> --disablerepo="update"
```

---

## 四、更新与维护说明

### 1. 本地源的内容是静态的

本地光盘源只包含制作该 ISO 镜像时的软件包，**无法通过 `dnf update` 自动升级**。

若需更新软件版本，应前往官网下载**最新 ISO 镜像**，重新执行"挂载 → 复制到硬盘 → 配置 repo → 刷新缓存"流程。

### 2. 恢复在线更新

当服务器后续接入互联网、需要使用官方在线源时，重新启用之前禁用的源即可：

```bash
sudo yum-config-manager --enable update
sudo dnf update -y
```

---

## 五、常用命令清单

| 操作         | 命令                                                |
|--------------|-----------------------------------------------------|
| 挂载镜像     | `mount /dev/cdrom /mnt/cdrom`                       |
| 复制到硬盘   | `cp -r /mnt/cdrom/* /data/everything/`              |
| 定位索引目录 | `find /data/ -type d -name repodata`                |
| 清理缓存     | `sudo dnf clean all`                                |
| 刷新源       | `sudo dnf makecache`                                |
| 本地源安装   | `sudo dnf install <包名> -y --disablerepo="update"` |
| 禁用在线源   | `sudo yum-config-manager --disable update`          |
| 启用在线源   | `sudo yum-config-manager --enable update`           |
| 在线系统升级 | `sudo dnf update -y`                                |

---

> 提示：本地环境搭建完成后，可继续配置 SSH 服务的开机自启，并从 Windows 端远程连接管理。
