#ifndef MPI_LOG_H
#define MPI_LOG_H

#include <iostream>
// 全局日志文件流
extern std::ofstream g_logFile;
extern int g_worldRank;

std::string getCurrentTimestamp();

// 日志输出函数
void logMessage(const std::string& message);

void rank0log(const std::string& message);

#endif