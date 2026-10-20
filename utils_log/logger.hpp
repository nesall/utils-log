// Author: Arman Sahakyan
#pragma once
#include <iostream>
#include <fstream>
#include <sstream>
#include <mutex>
#include <string>
#include <string_view>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <thread>
#include <atomic>
#include <cstdint>
#include <ctime>
#include <utility>
#include <cstdio>
#include <system_error>

#ifdef QT_CORE_LIB
#include <QString>
#include <QStringList>
#include <QDebug>
#endif // QT_CORE_LIB

#ifdef _WIN32
extern "C" __declspec(dllimport) void __stdcall OutputDebugStringA(const char *);
#endif


namespace utils_log {

  enum class Level : int { debug, info, warn, error };

  namespace impl {
    inline std::atomic<Level> minLevel{ Level::info };
    inline std::string outputFilePath = "output.log";
    inline std::string diagnosticsFilePath = "diagnostics.log";

    inline std::atomic_bool logToFile{ true };
    inline std::atomic_bool logToConsole{ true };

    std::string dateTime() {
      using namespace std::chrono;
      const auto now = system_clock::now();
      const auto t = system_clock::to_time_t(now);
      const auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
      std::tm tm{};
#ifdef _WIN32
      localtime_s(&tm, &t);
#else
      localtime_r(&t, &tm);
#endif
      char buf[32];
      std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
      char result[32];
      std::snprintf(result, sizeof(result), "%s.%03lld", buf, static_cast<long long>(ms));
      return result;
    }

  }

#define SET_LOG_MIN_LEVEL(x) utils_log::impl::minLevel = (x)
#define SET_LOG_OUTPUT_FILE_PATH(x) utils_log::Log::setOutputFile(x)
#define SET_LOG_DIAGNOSTICS_FILE_PATH(x) utils_log::ScopeLogger::setFile(x)

#define SET_LOG_TO_FILE(x) utils_log::impl::logToFile = (x)
#define SET_LOG_TO_CONSOLE(x) utils_log::impl::logToConsole = (x)


  // ============================================================================
  //                                Log
  // ============================================================================

  class Log {
  public:
    struct NospaceTag {};
    struct SpaceTag {};
    
  public:
    Log(Level lvl, bool toFile = impl::logToFile.load(), bool toConsole = impl::logToConsole.load())
      : level_(lvl), toFile_(toFile), toConsole_(toConsole) {
    }

    ~Log() { 
      try {
        commit();
      } catch (...) {}
    }

    template <typename T>
    Log &operator<<(T &&val) {
      if (hasLog_ && !noSpace_) ss_ << ' ';
      ss_ << std::forward<T>(val);
      hasLog_ = true;
      return *this;
    }
    
    Log &operator<<(Log::NospaceTag) {
      noSpace_ = true;
      return *this;
    }

    Log &operator<<(Log::SpaceTag) {
      noSpace_ = false;
      return *this;
    }

    Log &noquote() { return *this; }
    
    void commit() {
      if (!hasLog_) return;
      const std::string msg = ss_.str();
      ss_.str({});
      ss_.clear();
      hasLog_ = false;

      const auto line = "[" + impl::dateTime() + "] " + levelStr(level_) + " tid=" + std::to_string(static_cast<unsigned long long>(threadId())) + " \"" + msg + "\"";
      state().runningFileSize += (line.size() + 1);

      std::scoped_lock lock(state().mutex);
      if (toFile_) {
        ensureFileOpen();
        if (state().fout.good()) {
          state().fout << line << '\n';
          state().fout.flush();
          state().runningFileSize += line.size() + 1;
          if (state().runningFileSize > kMaxFileSize_) state().fout.close();  // next commit reopens and rotates
        }
      }

      // Console log (always)
#ifdef QT_CORE_LIB
      if (toConsole_) {
        if (level_ == Level::warn)
          qWarning().nospace().noquote() << line.c_str();
        else if (level_ == Level::error)
          qCritical().nospace().noquote() << line.c_str();
        else
          qDebug().nospace().noquote() << line.c_str();
      }
#else
      if (toConsole_) {
        if (level_ == Level::error || level_ == Level::warn) {
          std::cerr << line << '\n';
        } else {
          std::cout << line << '\n';
        }
#ifdef _WIN32
        ::OutputDebugStringA((line + "\n").c_str());
#endif // _WIN32
      }
#endif // QT_CORE_LIB
    }

    static void terminate() {
      std::scoped_lock lock(state().mutex);
      if (state().fout.is_open()) state().fout.close();
    }

  private:
    struct State {
      std::mutex mutex;
      std::ofstream fout;
      size_t runningFileSize = 0;
    };

    static State &state() {
      static State &s = *new State;   // built once (thread-safe), never destroyed
      return s;
    }

  private:
    Level level_ = Level::info;
    bool toFile_;
    bool toConsole_;
    bool hasLog_ = false;
    bool noSpace_ = false;
    std::ostringstream ss_;

    static constexpr size_t kMaxFileSize_ = 5 * 1024 * 1024;

  private:
    static const char *levelStr(Level l) {
      switch (l) {
      case Level::debug: return "[DEBUG]";
      case Level::error: return "[ERROR]";
      case Level::warn: return "[WARN ]";
      default: return "[INFO ]";
      }
    }

    static uint64_t threadId() {
      thread_local static const uint64_t id = [] {
        std::ostringstream oss;
        oss << std::this_thread::get_id();
        return static_cast<uint64_t>(
          std::hash<std::string>{}(oss.str()));
        }();
      return id;
    }

    static void ensureFileOpen() {
      if (state().fout.is_open()) return;
      const std::string &fname = impl::outputFilePath;
      rotateIfTooLarge(fname, kMaxFileSize_);
      state().fout.open(fname, std::ios::app);
      std::error_code ec;
      const auto sz = state().fout.is_open() ? std::filesystem::file_size(fname, ec) : 0;
      state().runningFileSize = ec ? 0 : static_cast<size_t>(sz);  // file_size returns -1 on error
    }

  public:
    static void setOutputFile(std::string path) {
      std::scoped_lock lock(state().mutex);
      impl::outputFilePath = std::move(path);
      if (state().fout.is_open()) state().fout.close();
    }

    static bool rotateIfTooLarge(const std::string &fname, uintmax_t maxSize) {
      namespace fs = std::filesystem;
      std::error_code ec;
      const auto sz = fs::file_size(fname, ec);
      if (ec || sz <= maxSize) return false;
      fs::remove(fname + ".old", ec);   // missing file isn't an error here
      ec.clear();
      fs::rename(fname, fname + ".old", ec);
      return !ec;
    }
  };

inline constexpr Log::NospaceTag LOGNOSPACE{};
inline constexpr Log::SpaceTag LOGSPACE{};

#define LOG_NOSPACE utils_log::LOGNOSPACE
#define LOG_SPACE utils_log::LOGSPACE

#define LOG_MSG_IF(lvl) if (lvl < utils_log::impl::minLevel.load()) {} else utils_log::Log(lvl)
#define LOG_MSG_DEBUG LOG_MSG_IF(utils_log::Level::debug)
#define LOG_MSG_INFO  LOG_MSG_IF(utils_log::Level::info)
#define LOG_MSG_WARN  LOG_MSG_IF(utils_log::Level::warn)
#define LOG_MSG_ERROR LOG_MSG_IF(utils_log::Level::error)

#define LOG_MSG LOG_MSG_INFO
#define LOG_MSGNF utils_log::Log(utils_log::Level::info, false)


  // ============================================================================
  //                            ScopeLogger (diagnostics.log)
  // ============================================================================
  class ScopeLogger {
  public:
    ScopeLogger(std::string_view func, std::string_view file, int line)
      : func_(func), file_(file), line_(line) {
      log("start...", +1);
    }

    ScopeLogger(std::string_view func, std::string_view name, std::string_view file, int line)
      : funcStore_(std::string(func) + ":" + std::string(name)), func_(funcStore_), file_(file), line_(line) {
      log("start...", +1);
    }

    ScopeLogger(const ScopeLogger &) = delete;
    ScopeLogger &operator = (const ScopeLogger &) = delete;

    ~ScopeLogger() {
      log("end!", -1);
    }

    void here(std::string_view msg) { log(msg); }

  private:
    struct State {
      std::atomic<int> count{ 0 };
      std::ofstream fout;
      std::mutex mutex;
      bool initialized = false;
      bool crashedLastTime = false;
      bool crashChecked = false;
    };
    static State &state() {
      static State &s = *new State;   // built once (thread-safe), never destroyed
      return s;
    }

  private:
    std::string funcStore_;
    std::string_view func_;
    std::string_view file_;
    int line_;


    static void rotateIfTooLarge(const std::string &fname, size_t maxSize) {
      Log::rotateIfTooLarge(fname, maxSize);
    }

    static std::string lastLine(const std::string &fname) {
      std::ifstream ifs(fname);
      if (!ifs.is_open()) return {};
      std::string line, last;
      while (std::getline(ifs, line)) last = line;
      return last;
    }

    static bool detectPreviousCrash() {
      if (state().crashChecked) return state().crashedLastTime;
      state().crashChecked = true;

      const auto &fname = impl::diagnosticsFilePath;
      if (!std::filesystem::exists(fname)) return false;

      const auto line = lastLine(fname);
      const auto pos = line.rfind('|');
      if (pos == std::string::npos) return false;

      try {
        int n = std::stoi(line.substr(pos + 1));
        state().crashedLastTime = (n > 0);
      } catch (...) {
        state().crashedLastTime = false;
      }
      return state().crashedLastTime;
    }

    static void ensureFileOpen() {
      if (!state().initialized) {
        bool crashed = detectPreviousCrash();
        const std::string &fname = impl::diagnosticsFilePath;
        rotateIfTooLarge(fname, 2ull * 1024 * 1024);
        state().fout.open(fname, std::ios::app);
        state().initialized = true;
        if (crashed) {
          state().fout << "## CRASH POINT ##\n";
          state().fout.flush();
        }
      } else if (!state().fout.is_open()) {
        state().fout.open(impl::diagnosticsFilePath, std::ios::app);
      }
    }

    void log(std::string_view phase, int8_t inc = 0) const {
      std::scoped_lock lock(state().mutex);
      if (inc) state().count += inc;
      ensureFileOpen();
      const auto msg = ("[" + impl::dateTime() + "] " + 
        std::string{ func_ } + ":" + std::string{ phase } + " " + std::string{ file_ } + " |" + std::to_string(state().count.load()) + "\n");
      if (state().fout.good()) {
        state().fout << msg;
        state().fout.flush();
      }
    }

  public:
    static void setFile(std::string path) {
      std::scoped_lock lock(state().mutex);
      impl::diagnosticsFilePath = std::move(path);
      if (state().fout.is_open()) state().fout.close();
      state().initialized = false;
      state().crashChecked = false;   // otherwise the new file is never crash-checked
    }
  };

  // Macros
#define LOG_START utils_log::ScopeLogger _scopelog_(__func__, __FILE__, __LINE__)
#define LOG_START1(x) utils_log::ScopeLogger _scopelog_(__func__, x, __FILE__, __LINE__)
#define LOG_HERE(x) _scopelog_.here(x)

} // namespace utils_log
