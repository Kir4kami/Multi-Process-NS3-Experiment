#!/bin/bash
# 实时验证 MPI 进程的 CPU 绑定情况

if [ $# -ge 1 ]; then
    SEARCH_TERM=$1
else
    SEARCH_TERM="ns3.*cut-mpi"
fi

echo "=========================================="
echo "MPI 进程 CPU 绑定验证工具"
echo "=========================================="
echo "查找进程: $SEARCH_TERM"
echo ""

# 查找进程
pids=$(pgrep -f "$SEARCH_TERM")

if [ -z "$pids" ]; then
    echo "未找到匹配的进程"
    echo ""
    echo "提示：先运行你的 MPI 程序，然后在另一个终端运行此脚本"
    echo "或者指定进程名: $0 ns3"
    exit 1
fi

echo "找到以下进程："
echo ""

for pid in $pids; do
    if ! ps -p $pid > /dev/null 2>&1; then
        continue
    fi
    
    # 获取进程信息
    cmd=$(ps -p $pid -o cmd= 2>/dev/null | head -c 60)
    psr=$(ps -p $pid -o psr= 2>/dev/null | tr -d ' ')
    
    # 获取 CPU 亲和性
    cpu_affinity=$(taskset -p $pid 2>/dev/null | awk '{print $NF}')
    
    # 从 /proc 获取详细信息
    if [ -f "/proc/$pid/status" ]; then
        cpus_allowed=$(grep "Cpus_allowed:" /proc/$pid/status | awk '{print $2}')
        cpus_list=$(grep "Cpus_allowed_list:" /proc/$pid/status | awk '{print $2}')
    else
        cpus_allowed="N/A"
        cpus_list="N/A"
    fi
    
    echo "PID: $pid"
    echo "  命令: $cmd..."
    echo "  当前运行在 CPU 核心: $psr"
    echo "  CPU 亲和性掩码: $cpu_affinity"
    echo "  允许的 CPU 核心列表: $cpus_list"
    echo ""
done

echo "=========================================="
echo "持续监控（每2秒刷新，按 Ctrl+C 退出）"
echo "=========================================="

# 持续监控
while true; do
    clear
    echo "=========================================="
    echo "MPI 进程 CPU 绑定监控 - $(date '+%H:%M:%S')"
    echo "=========================================="
    echo ""
    
    found=0
    for pid in $pids; do
        if ! ps -p $pid > /dev/null 2>&1; then
            continue
        fi
        
        psr=$(ps -p $pid -o psr= 2>/dev/null | tr -d ' ')
        cpus_list=$(grep "Cpus_allowed_list:" /proc/$pid/status 2>/dev/null | awk '{print $2}')
        
        if [ -n "$cpus_list" ]; then
            printf "PID %-6s | 当前CPU: %-3s | 允许的CPU: %s\n" "$pid" "$psr" "$cpus_list"
            found=1
        fi
    done
    
    if [ $found -eq 0 ]; then
        echo "所有进程已结束"
        break
    fi
    
    echo ""
    echo "按 Ctrl+C 退出监控"
    sleep 2
done

