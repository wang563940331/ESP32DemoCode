#!/usr/bin/env bash
# 将本地 OTA 固件包上传到 FTP
# 远程文件名：RemoteControlO<版本号>.bin（版本=分支-短哈希，与固件 APP_VERSION_FULL 一致）
#
# 机密不进仓库：从环境变量或本地 .env 读取（.env 已在 .gitignore）
#   OTA_FTP_HOST / OTA_FTP_PORT / OTA_FTP_USER / OTA_FTP_PASS / OTA_FTP_DIR
#
# 用法：
#   ./otapush.sh                              # 上传 build/RemoteControlO_Com.bin
#   ./otapush.sh path/to/xxx.bin              # 指定本地文件，远程名仍按版本号
#   ./otapush.sh path/to/xxx.bin NAME.bin     # 指定本地文件与远程文件名
#   ./otapush.sh path/to/xxx.bin NAME.bin VER # 同上，并显式传入版本号

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 可选加载本地密钥文件（勿提交）
if [[ -f "${ROOT_DIR}/.env" ]]; then
    set -a
    # shellcheck disable=SC1091
    source "${ROOT_DIR}/.env"
    set +a
fi

FTP_HOST="${OTA_FTP_HOST:-}"
FTP_PORT="${OTA_FTP_PORT:-40009}"
FTP_USER="${OTA_FTP_USER:-}"
FTP_PASS="${OTA_FTP_PASS:-}"
FTP_REMOTE_DIR="${OTA_FTP_DIR:-/ESP32OTA}"

if [[ -z "${FTP_HOST}" || -z "${FTP_USER}" || -z "${FTP_PASS}" ]]; then
    echo "[ERROR] 未配置 FTP 机密。请导出环境变量或在工程根目录放置 .env："
    echo "        OTA_FTP_HOST=..."
    echo "        OTA_FTP_PORT=40009"
    echo "        OTA_FTP_USER=..."
    echo "        OTA_FTP_PASS=..."
    echo "        OTA_FTP_DIR=/ESP32OTA"
    exit 1
fi

# 本地产物仍是工程名 bin；远程按版本命名
DEFAULT_BIN="${ROOT_DIR}/build/RemoteControlO_Com.bin"
LOCAL_FILE="${1:-${DEFAULT_BIN}}"

# 解析与固件一致的版本号（分支-8位短哈希）
resolve_app_version() {
    local branch hash
    if ! command -v git >/dev/null 2>&1; then
        echo "unknown-unknown"
        return
    fi
    branch="$(git -C "${ROOT_DIR}" rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)"
    hash="$(git -C "${ROOT_DIR}" rev-parse --short=8 "origin/${branch}" 2>/dev/null || true)"
    if [[ -z "${hash}" ]]; then
        hash="$(git -C "${ROOT_DIR}" rev-parse --short=8 HEAD 2>/dev/null || echo unknown)"
    fi
    echo "${branch}-${hash}"
}

APP_VER="${3:-}"
if [[ -z "${APP_VER}" ]]; then
    APP_VER="$(resolve_app_version)"
fi

if [[ -n "${2:-}" ]]; then
    REMOTE_NAME="$(basename "${2}")"
else
    REMOTE_NAME="RemoteControlO${APP_VER}.bin"
fi

if [[ ! -f "${LOCAL_FILE}" ]]; then
    echo "[ERROR] 找不到固件文件: ${LOCAL_FILE}"
    echo "        请先 idf.py build，或传入显式路径: ./otapush.sh xxx.bin"
    exit 1
fi

FILE_SIZE="$(wc -c < "${LOCAL_FILE}" | tr -d ' ')"

echo "[INFO] 本地文件: ${LOCAL_FILE} (${FILE_SIZE} bytes)"
echo "[INFO] 版本号:   ${APP_VER}"
echo "[INFO] 目标: ftp://${FTP_HOST}:${FTP_PORT}${FTP_REMOTE_DIR}/${REMOTE_NAME}"
echo "[INFO] 用户: ${FTP_USER}"

if command -v curl >/dev/null 2>&1; then
    curl --fail --show-error --progress-bar \
        --ftp-create-dirs \
        --user "${FTP_USER}:${FTP_PASS}" \
        -T "${LOCAL_FILE}" \
        "ftp://${FTP_HOST}:${FTP_PORT}${FTP_REMOTE_DIR}/${REMOTE_NAME}"
    echo "[OK] 上传完成(curl): ${FTP_REMOTE_DIR}/${REMOTE_NAME}"
    exit 0
fi

if command -v lftp >/dev/null 2>&1; then
    lftp -u "${FTP_USER},${FTP_PASS}" -p "${FTP_PORT}" "ftp://${FTP_HOST}" <<EOF
set ftp:passive-mode true
mkdir -p ${FTP_REMOTE_DIR}
cd ${FTP_REMOTE_DIR}
put ${LOCAL_FILE} -o ${REMOTE_NAME}
bye
EOF
    echo "[OK] 上传完成(lftp): ${FTP_REMOTE_DIR}/${REMOTE_NAME}"
    exit 0
fi

if command -v python3 >/dev/null 2>&1; then
    OTA_FTP_HOST="${FTP_HOST}" OTA_FTP_PORT="${FTP_PORT}" \
    OTA_FTP_USER="${FTP_USER}" OTA_FTP_PASS="${FTP_PASS}" \
    OTA_FTP_DIR="${FTP_REMOTE_DIR}" \
    python3 - "${LOCAL_FILE}" "${REMOTE_NAME}" <<'PY'
import os
import sys
from ftplib import FTP
from pathlib import Path

host = os.environ["OTA_FTP_HOST"]
port = int(os.environ.get("OTA_FTP_PORT", "40009"))
user = os.environ["OTA_FTP_USER"]
passwd = os.environ["OTA_FTP_PASS"]
remote_dir = os.environ.get("OTA_FTP_DIR", "/ESP32OTA")
local_file = sys.argv[1]
remote_name = sys.argv[2]

ftp = FTP()
ftp.connect(host, port, timeout=30)
ftp.login(user, passwd)
ftp.set_pasv(True)

parts = [p for p in remote_dir.split("/") if p]
for part in parts:
    try:
        ftp.cwd(part)
    except Exception:
        ftp.mkd(part)
        ftp.cwd(part)

size = Path(local_file).stat().st_size
sent = 0

def _cb(chunk: bytes) -> None:
    global sent
    sent += len(chunk)
    pct = (sent * 100) // size if size else 100
    print(f"\r[INFO] 上传进度: {pct}% ({sent}/{size})", end="", flush=True)

with open(local_file, "rb") as f:
    ftp.storbinary(f"STOR {remote_name}", f, 8192, _cb)
print()
ftp.quit()
print(f"[OK] 上传完成(python): {remote_dir}/{remote_name}")
PY
    exit 0
fi

echo "[ERROR] 需要 curl、lftp 或 python3"
exit 1
