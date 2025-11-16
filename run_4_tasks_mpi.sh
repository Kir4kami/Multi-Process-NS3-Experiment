#!/bin/bash
# 并行运行4个MPI任务，每个任务使用2个核心

echo "=========================================="
echo "并行运行4个MPI任务（每个任务2个进程，128拓扑）"
echo "=========================================="

# 定义4个任务及其对应的CPU核心（每个任务2个核心）
# TP、EP、PP、DP对应的operate范围（128拓扑）
declare -a OPERATE_RANGES=("0-7" "8-15" "16-23" "160-175")
declare -a CPU_CORES=("2 3" "4 5" "6 7" "8 9")  # 每个任务的核心列表（空格分隔，2个核心）
declare -a TASK_NAMES=("任务1(TP)" "任务2(EP)" "任务3(PP)" "任务4(DP)")

# 创建日志目录
mkdir -p logs

echo "配置信息:"
for i in "${!OPERATE_RANGES[@]}"; do
    echo "  ${TASK_NAMES[$i]}: operate ${OPERATE_RANGES[$i]}, CPU核心 ${CPU_CORES[$i]}"
done
echo ""

# 预构建
echo "预构建NS-3项目..."
./ns3 build > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "预构建失败，退出"
    exit 1
fi
echo "预构建完成"
echo ""

# 启动所有任务的函数
start_task() {
    local range=$1
    local cpu_cores=$2  # 空格分隔的核心列表，如 "2 3"
    local task_name=$3
    local task_id=$4
    
    # 解析范围
    local start=$(echo $range | cut -d'-' -f1)
    local end=$(echo $range | cut -d'-' -f2)
    
    local log_file="logs/operate_${range}.log"
    
    # 解析CPU核心列表
    local core_array=($cpu_cores)
    local core1=${core_array[0]}
    local core2=${core_array[1]}
    
    echo "[$(date '+%H:%M:%S')] 启动${task_name}: operate ${range}, CPU核心 ${core1},${core2}"
    
    # 方法：通过环境变量NS3_CPU_SET传递CPU核心信息给ns3脚本
    # ns3脚本会根据MPI rank自动选择对应的CPU核心
    # 每个任务使用2个进程，分别绑定到2个CPU核心
    (
        # 设置环境变量，ns3脚本会根据MPI rank自动绑定到对应CPU
        # rank 0使用第一个CPU核心，rank 1使用第二个CPU核心
        export NS3_CPU_SET=${core1},${core2}
        
        # 内存监控日志文件
        local mem_log_file="logs/memory_${range}.log"
        echo "[$(date '+%H:%M:%S')] 开始监控内存使用（2进程模式，128拓扑）" > "$mem_log_file"
        
        # 启动mpirun，使用2个进程
        mpirun -np 2 \
            --mca btl_vader_single_copy_mechanism none \
            ./ns3 run "cut-mpi --operateStart=$start --operateEnd=$end" \
            > "${log_file}" 2>&1 &
        MPIRUN_PID=$!
        
        # 等待进程启动
        sleep 3
        
        # 监控内存使用（每5秒记录一次，直到进程结束）
        MAX_RSS=0
        MAX_VIRT=0
        while ps -p $MPIRUN_PID > /dev/null 2>&1; do
            # 查找ns3子进程
            NS3_PIDS=()
            for pid in $(pgrep -f "ns3.*cut-mpi.*operateStart=$start" 2>/dev/null); do
                if ps -p $pid > /dev/null 2>&1 && [[ $(ps -p $pid -o comm=) == *"ns3"* ]] && [[ $(ps -p $pid -o cmd=) != *"python"* ]]; then
                    NS3_PIDS+=($pid)
                fi
            done
            
            # 记录每个进程的内存使用
            TOTAL_RSS=0
            TOTAL_VIRT=0
            for pid in "${NS3_PIDS[@]}"; do
                if ps -p $pid > /dev/null 2>&1; then
                    # 获取内存信息（KB）
                    rss=$(ps -p $pid -o rss= 2>/dev/null | tr -d ' ')
                    virt=$(ps -p $pid -o vsz= 2>/dev/null | tr -d ' ')
                    if [ -n "$rss" ] && [ -n "$virt" ]; then
                        TOTAL_RSS=$((TOTAL_RSS + rss))
                        TOTAL_VIRT=$((TOTAL_VIRT + virt))
                    fi
                fi
            done
            
            # 更新峰值
            if [ $TOTAL_RSS -gt $MAX_RSS ]; then
                MAX_RSS=$TOTAL_RSS
            fi
            if [ $TOTAL_VIRT -gt $MAX_VIRT ]; then
                MAX_VIRT=$TOTAL_VIRT
            fi
            
            # 记录到内存日志（每5秒）
            if [ ${#NS3_PIDS[@]} -gt 0 ]; then
                echo "[$(date '+%H:%M:%S')] PID列表: ${NS3_PIDS[@]}, RSS总和: ${TOTAL_RSS}KB (${MAX_RSS}KB峰值), VIRT总和: ${TOTAL_VIRT}KB (${MAX_VIRT}KB峰值)" >> "$mem_log_file"
            fi
            
            sleep 5
        done
        
        # 等待mpirun完成
        wait $MPIRUN_PID
        
        # 记录最终内存统计
        echo "" >> "$mem_log_file"
        echo "[$(date '+%H:%M:%S')] 任务完成" >> "$mem_log_file"
        echo "峰值RSS内存: ${MAX_RSS}KB ($(echo "scale=2; $MAX_RSS/1024" | bc)MB)" >> "$mem_log_file"
        echo "峰值VIRT内存: ${MAX_VIRT}KB ($(echo "scale=2; $MAX_VIRT/1024" | bc)MB)" >> "$mem_log_file"
        
        echo "[$(date '+%H:%M:%S')] ${task_name}完成: operate ${range}, 峰值内存: ${MAX_RSS}KB"
    ) &
    
    return $!
}

# 启动所有任务
echo "开始启动所有任务..."
echo ""

PIDS=()
for i in "${!OPERATE_RANGES[@]}"; do
    start_task "${OPERATE_RANGES[$i]}" "${CPU_CORES[$i]}" "${TASK_NAMES[$i]}" "$((i+1))"
    PIDS+=($!)
    sleep 1  # 稍微延迟，避免同时启动造成资源竞争
done

echo ""
echo "所有任务已启动，PID列表: ${PIDS[@]}"
echo ""
echo "=========================================="
echo "任务监控"
echo "=========================================="
echo "查看日志:"
for i in "${!OPERATE_RANGES[@]}"; do
    echo "  ${TASK_NAMES[$i]} (${OPERATE_RANGES[$i]}): tail -f logs/operate_${OPERATE_RANGES[$i]}.log"
done
echo ""
echo "查看内存监控:"
for i in "${!OPERATE_RANGES[@]}"; do
    echo "  ${TASK_NAMES[$i]} (${OPERATE_RANGES[$i]}): tail -f logs/memory_${OPERATE_RANGES[$i]}.log"
done
echo ""
echo "验证CPU绑定: ./verify_cpu_binding.sh"
echo ""
echo "等待所有任务完成..."
echo ""

# 等待所有后台任务完成
FAILED=0
for i in "${!PIDS[@]}"; do
    wait ${PIDS[$i]}
    EXIT_CODE=$?
    if [ $EXIT_CODE -ne 0 ]; then
        echo "警告: ${TASK_NAMES[$i]} (${OPERATE_RANGES[$i]}) 退出码: $EXIT_CODE"
        FAILED=$((FAILED + 1))
    fi
done

echo ""
echo "=========================================="
echo "所有任务完成"
echo "=========================================="

if [ $FAILED -eq 0 ]; then
    echo "✓ 所有任务成功完成"
else
    echo "✗ 有 $FAILED 个任务失败"
    exit 1
fi

echo ""
echo "日志文件位置:"
for range in "${OPERATE_RANGES[@]}"; do
    echo "  运行日志: logs/operate_${range}.log"
    echo "  内存日志: logs/memory_${range}.log"
done
echo ""
echo "=========================================="
echo "内存使用统计"
echo "=========================================="
for range in "${OPERATE_RANGES[@]}"; do
    mem_log="logs/memory_${range}.log"
    if [ -f "$mem_log" ]; then
        echo "${range}:"
        tail -3 "$mem_log" | grep -E "(峰值|Peak)" || tail -1 "$mem_log"
        echo ""
    fi
done

