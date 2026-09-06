#!/usr/bin/env bash
# 释放 ESP 串口：结束占用指定端口的 idf_monitor / esptool 等进程
# 用法：
#   ./scripts/serial_free.sh              # 自动找 /dev/ttyUSB*，释放全部
#   ./scripts/serial_free.sh /dev/ttyUSB0 # 只释放指定口
#   ./scripts/serial_free.sh -l           # 仅列出占用，不杀进程
#   PORT=/dev/ttyUSB0 ./scripts/serial_free.sh

set -euo pipefail

LIST_ONLY=0
PORT_ARG=""

usage() {
    cat <<'EOF'
用法: serial_free.sh [选项] [端口]

  释放被串口监视器/烧录工具占用的 USB 串口。

选项:
  -l, --list     只列出占用进程，不结束
  -h, --help     显示帮助

示例:
  ./scripts/serial_free.sh
  ./scripts/serial_free.sh /dev/ttyUSB0
  ./scripts/serial_free.sh -l
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -l|--list) LIST_ONLY=1; shift ;;
        -h|--help) usage; exit 0 ;;
        -*)
            echo "未知选项: $1" >&2
            usage >&2
            exit 1
            ;;
        *)
            PORT_ARG="$1"
            shift
            ;;
    esac
done

# 解析要处理的端口列表
ports=()
if [[ -n "$PORT_ARG" ]]; then
    ports+=("$PORT_ARG")
elif [[ -n "${PORT:-}" ]]; then
    ports+=("$PORT")
else
    # 未指定则处理所有 ttyUSB*
    shopt -s nullglob
    for p in /dev/ttyUSB*; do
        ports+=("$p")
    done
    shopt -u nullglob
fi

if [[ ${#ports[@]} -eq 0 ]]; then
    echo "未找到串口设备（/dev/ttyUSB*）。请检查 USB 线或传端口参数。"
    exit 1
fi

# 结束占用某端口的进程
# 参数: $1=端口路径
free_one_port() {
    local port="$1"
    if [[ ! -e "$port" ]]; then
        echo "[跳过] $port 不存在"
        return 0
    fi

    echo "==== $port ===="
    # fuser 输出占用 PID；无占用时返回非 0
    local pids
    pids="$(fuser "$port" 2>/dev/null || true)"
    # 去掉空白，得到 PID 列表
    pids="$(echo "$pids" | tr -s '[:space:]' ' ' | sed 's/^ //;s/ $//')"

    if [[ -z "$pids" ]]; then
        echo "  空闲"
        return 0
    fi

    echo "  占用 PID: $pids"
    # 打印命令行，方便确认是不是监视器
    for pid in $pids; do
        if [[ -r "/proc/$pid/cmdline" ]]; then
            local cmd
            cmd="$(tr '\0' ' ' <"/proc/$pid/cmdline" | sed 's/ $//')"
            echo "  PID $pid: $cmd"
        fi
    done

    if [[ "$LIST_ONLY" -eq 1 ]]; then
        return 0
    fi

    # 先温和结束，再强杀残留
    # shellcheck disable=SC2086
    kill $pids 2>/dev/null || true
    sleep 0.4
    local left
    left="$(fuser "$port" 2>/dev/null || true)"
    if [[ -n "$(echo "$left" | tr -d '[:space:]')" ]]; then
        # shellcheck disable=SC2086
        kill -9 $left 2>/dev/null || true
        sleep 0.2
    fi

    if fuser "$port" >/dev/null 2>&1; then
        echo "  仍被占用，请手动检查: fuser -v $port"
        return 1
    fi
    echo "  已释放"
    return 0
}

fail=0
for p in "${ports[@]}"; do
    if ! free_one_port "$p"; then
        fail=1
    fi
done

if [[ "$fail" -ne 0 ]]; then
    exit 1
fi

if [[ "$LIST_ONLY" -eq 0 ]]; then
    echo "完成。可重新烧录/监视，例如:"
    echo "  idf.py -p ${ports[0]} flash monitor"
fi
