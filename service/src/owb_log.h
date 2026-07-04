// service/src/owb_log.h
//
// Logger de archivo del servicio: %ProgramData%\OpenWinBlue\logs\<componente>.log
// + OutputDebugString (DebugView) + stdout (modo consola).
//
#pragma once

namespace owb::log {

enum class Level { Debug, Info, Warn, Error };

void init(const char* component);
void shutdown();
void write(Level level, const char* fmt, ...);

} // namespace owb::log

#define OWB_LOG_DEBUG(...) ::owb::log::write(::owb::log::Level::Debug, __VA_ARGS__)
#define OWB_LOG_INFO(...)  ::owb::log::write(::owb::log::Level::Info,  __VA_ARGS__)
#define OWB_LOG_WARN(...)  ::owb::log::write(::owb::log::Level::Warn,  __VA_ARGS__)
#define OWB_LOG_ERROR(...) ::owb::log::write(::owb::log::Level::Error, __VA_ARGS__)
