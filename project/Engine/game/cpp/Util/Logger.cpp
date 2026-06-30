#include "Logger.h"

namespace engine {
namespace Logger {
void Log(const std::string& message)
{
    std::string finalMessage = message + "\n";
    OutputDebugStringA(message.c_str());
}
}
} // namespace engine