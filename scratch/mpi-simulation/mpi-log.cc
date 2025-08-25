#include "mpi-log.h"
#include "mpi-fixtures.h"
#include <iomanip>
#include <sstream>
#include <fstream>
#include <iostream> 

std::ofstream g_logFile;
int g_worldRank = 0;
std::string getCurrentTimestamp() {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// 日志输出函数
void logMessage(const std::string& message) {
    std::string timestamp = getCurrentTimestamp();
    g_logFile<< "[" <<  timestamp << "] "  << "[Rank " << g_worldRank << "]" << message << std::endl;
    g_logFile.flush();
}
//只有0进程打印
void rank0log(const std::string& message) {
    if (g_worldRank == 0) {
        logMessage(message);
    }
}