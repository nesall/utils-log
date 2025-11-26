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
//#include <format>
#include <utility>
#include <limits>

#ifdef QT_CORE_LIB
#include <QString>
#include <QStringList>
#include <QDebug>
#endif // QT_CORE_LIB

#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#include "windows.h"
#endif


namespace utils_log {

  //template <typename... Args>
  //std::string strf(Args&&... args) {
  //  std::ostringstream oss;
  //  (oss << ... << std::forward<Args>(args));
  //  return oss.str();
  //}

  namespace impl {
    inline std::string outputFilePath = "output.log";
    inline std::string diagnosticsFilePath = "diagnostics.log";

    inline std::atomic_bool logToFile{ true };
    inline std::atomic_bool logToConsole{ true };
  }

#define SET_LOG_OUTPUT_FILE_PATH(x) utils_log::impl::outputFilePath = (x)
#define SET_LOG_DIAGNOSTICS_FILE_PATH(x) utils_log::impl::diagnosticsFilePath = (x)

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
    Log(bool toFile = impl::logToFile.load(), bool toConsole = impl::logToConsole.load())
      : toFile_(toFile), toConsole_(toConsole) {
    }

    ~Log() { commit(); }

    template <typename T>
    Log &operator<<(T &&val) {
      if (hasLog_ && !noSpace_) ss_ << ' ';
      ss_ << std::forward<T>(val);
      hasLog_ = true;
      return *this;
    }

    Log &operator<<(std::string_view sv) {
      if (hasLog_ && !noSpace_) ss_ << ' ';
      ss_ << sv;
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

      //const auto line = std::format("[{}] tid={} \"{}\"", dateTime(), threadId(), msg);
      const auto line = "[" + dateTime() + "] tid=" + std::to_string(static_cast<unsigned long long>(threadId())) + " \"" + msg + "\"";

      std::scoped_lock lock(globalMutex());
      if (toFile_) {
        ensureFileOpen();
        if (fout_.good()) {
          fout_ << line << '\n';
          fout_.flush();
        }
      }

      // Console log (always)
#ifdef QT_CORE_LIB
      if (toConsole_) {
        qDebug().nospace().noquote() << line.c_str();
      }
#else
      if (toConsole_) {
        std::cout << msg << std::endl;
#ifdef _MSC_VER
        ::OutputDebugStringA((msg + "\n").c_str());
#endif // _MSC_VER
      }
#endif // QT_CORE_LIB
    }

    static void terminate() {
      std::scoped_lock lock(globalMutex());
      if (fout_.is_open()) fout_.close();
    }

  private:
    bool toFile_;
    bool toConsole_;
    bool hasLog_ = false;
    bool noSpace_ = false;
    std::ostringstream ss_;

    static inline std::ofstream fout_;
    static inline std::atomic<bool> initialized_ = false;

    static inline std::mutex &globalMutex() {
      static std::mutex m;
      return m;
    }

    static std::string dateTime() {
      using namespace std::chrono;
      const auto now = system_clock::now();
      const auto t = system_clock::to_time_t(now);
      std::tm tm{};
#ifdef _WIN32
      localtime_s(&tm, &t);
#else
      localtime_r(&t, &tm);
#endif
      std::ostringstream oss;
      oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
      return oss.str();
    }

    static uint64_t threadId() {
      auto id = std::this_thread::get_id();
      std::ostringstream oss;
      oss << id;
      return std::hash<std::string>{}(oss.str());
    }

    static void ensureFileOpen() {
      const std::string &fname = impl::outputFilePath;
      if (!initialized_) {
        rotateIfTooLarge(fname, 5 * 1024 * 1024);
        fout_.open(fname, std::ios::app);
        initialized_ = true;
      } else if (!fout_.is_open()) {
        fout_.open(fname, std::ios::app);
      }
    }

    static void rotateIfTooLarge(const std::string &fname, uintmax_t maxSize) {
      namespace fs = std::filesystem;
      if (fs::exists(fname) && fs::file_size(fname) > maxSize) {
        const auto backup = fname + ".old";
        if (fs::exists(backup)) fs::remove(backup);
        fs::rename(fname, backup);
      }
    }
  };

inline constexpr Log::NospaceTag LOGNOSPACE{};
inline constexpr Log::SpaceTag LOGSPACE{};

#define LOG_NOSPACE utils_log::LOGNOSPACE
#define LOG_SPACE utils_log::LOGSPACE

#define LOG_MSG utils_log::Log()
#define LOG_MSGNF utils_log::Log(false)


  // ============================================================================
  //                            ScopeLogger (diagnostics.log)
  // ============================================================================
  class ScopeLogger {
  public:
    ScopeLogger(std::string_view func, std::string_view file, int line)
      : func_(func), file_(file), line_(line) {
      init();
      log("start...");
      count_ ++;
    }

    ScopeLogger(std::string_view func, std::string_view name, std::string_view file, int line)
      : func_(
        //std::format("{}:{}", func, name)
        (std::string(func) + ":" + std::string(name))
      ), file_(file), line_(line) {
      init();
      log("start...");
      count_++;
    }

    ~ScopeLogger() {
      count_--;
      log("end!");
    }

    void here(std::string_view msg) { log(msg); }

  private:
    std::string func_;
    std::string file_;
    int line_;

    inline static std::atomic<int> count_{ 0 };
    inline static std::ofstream fout_;
    inline static std::mutex mutex_;
    inline static bool initialized_ = false;
    inline static bool crashedLastTime_ = false;
    inline static bool crashChecked_ = false;

    static std::string dateTime() {
      using namespace std::chrono;
      const auto now = system_clock::now();
      const auto t = system_clock::to_time_t(now);
      std::tm tm{};
#ifdef _WIN32
      localtime_s(&tm, &t);
#else
      localtime_r(&t, &tm);
#endif
      std::ostringstream oss;
      oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
      return oss.str();
    }

    static void rotateIfTooLarge(const std::string &fname, uintmax_t maxSize) {
      namespace fs = std::filesystem;
      if (fs::exists(fname) && maxSize < fs::file_size(fname)) {
        const auto backup = fname + ".old";
        if (fs::exists(backup)) fs::remove(backup);
        fs::rename(fname, backup);
      }
    }

    static std::string lastLine(const std::string &fname) {
      std::ifstream ifs(fname);
      if (!ifs.is_open()) return {};
      std::string line, last;
      while (std::getline(ifs, line)) last = line;
      return last;
    }

    static bool detectPreviousCrash() {
      if (crashChecked_) return crashedLastTime_;
      crashChecked_ = true;

      const auto &fname = impl::diagnosticsFilePath;
      if (!std::filesystem::exists(fname)) return false;

      const auto line = lastLine(fname);
      const auto pos = line.rfind('|');
      if (pos == std::string::npos) return false;

      try {
        int n = std::stoi(line.substr(pos + 1));
        crashedLastTime_ = (n > 0);
      } catch (...) {
        crashedLastTime_ = false;
      }
      return crashedLastTime_;
    }

    static void ensureFileOpen() {
      if (!initialized_) {
        const std::string &fname = impl::diagnosticsFilePath;
        rotateIfTooLarge(fname, 2ull * 1024 * 1024);
        fout_.open(fname, std::ios::app);
        initialized_ = true;

        if (detectPreviousCrash()) {
          fout_ << "## CRASH POINT ##\n";
          fout_.flush();
        }
      } else if (!fout_.is_open()) {
        fout_.open(impl::diagnosticsFilePath, std::ios::app);
      }
    }

    void init() {
      std::scoped_lock lock(mutex_);
      ensureFileOpen();
    }

    void log(std::string_view phase) const {
      std::scoped_lock lock(mutex_);
      ensureFileOpen();
      //const auto msg = std::format("[{}] {}:{} {} |{}\n", dateTime(), func_, phase, file_, count_.load());
      const auto msg = ("[" + dateTime() + "] " + func_ + ":" + std::string(phase) + " " + file_ + " |" + std::to_string(count_.load()) + "\n");

      if (fout_.good()) {
        fout_ << msg;
        fout_.flush();
      }
    }
  };

  // Macros
#define LOG_START utils_log::ScopeLogger _scopelog_(__FUNCTION__, __FILE__, __LINE__)
#define LOG_START1(x) utils_log::ScopeLogger _scopelog_(__FUNCTION__, x, __FILE__, __LINE__)
#define LOG_HERE(x) _scopelog_.here(x)

} // namespace utils
