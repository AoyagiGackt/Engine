#include "Logger.h"

namespace engine {
void Logger::Log(const std::string& message)
{
    std::string finalMessage = message + "\n";
    OutputDebugStringA(finalMessage.c_str());
}
} // namespace engine
