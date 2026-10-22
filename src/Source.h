#pragma once

#include <mutex>
#include <string>
#include <vector>

// A diagnostic has been written and this translation unit is abandoned; the driver catches it per job.
struct CompileFailed {};
// Every diagnostic is written whole under this lock, so two jobs' messages never interleave.
std::mutex &diagnosticLock();
[[noreturn]] void compileFailed();

// Thrown by fail() inside a trial, and caught by whoever began it.
struct SubstitutionFailure {
    std::string why;
    // **A feature this compiler has not built is not a substitution failure.**
    bool unsupported;
    std::size_t pos;

    // C++11 has no aggregate with a member initialiser, so the braces call this.
    SubstitutionFailure(std::string why = std::string(), bool unsupported = false,
                        std::size_t pos = 0)
        : why(std::move(why)), unsupported(unsupported), pos(pos) {}
};

class Source {
public:
    struct Line {
        int file;
        int line;
    };
    // **`#pragma pack` in force from an output line on**: 0 is none. The
    // preprocessor records each change; the class layout asks at its keyword.
    struct Pack {
        std::size_t line;
        int value;
    };

    Source(std::string name, std::string text);
    Source(std::string name, std::string text, std::vector<std::string> files,
           std::vector<Line> lines);

    const std::string &text() const { return text_; }
    const char *begin() const { return text_.c_str(); }

    struct Place {
        int file;
        int line;
        int column;
    };
    Place locate(std::size_t pos) const;
    // `file:line:col` for a note that names a second place.
    std::string where(std::size_t pos) const;

    const std::vector<std::string> &files() const { return files_; }
    void setPacks(std::vector<Pack> packs) { packs_ = std::move(packs); }
    int packAt(std::size_t pos) const;

    static Source fromFile(const std::string &path);

    [[noreturn]] void fail(std::size_t pos, const std::string &message) const;

// **A trial: a stretch of parsing whose failure is an answer.** [temp.deduct]/8
// drops the specialization and issues no diagnostic. The one place here where a
// failure must not stop the compiler; everywhere else an error stops it.
    void beginTrial() const { trials_++; }
    void endTrial() const { trials_--; }

    // **-keep-going**: errors are counted, and the parser goes on from the next declaration
    // until `limit` of them; 0 is the default, where the first error ends the file.
    void setKeepGoing(int limit) { keepGoing_ = limit; }
    int keepGoing() const { return keepGoing_; }
    int errors() const { return errors_; }

private:
    mutable int trials_ = 0;
    int keepGoing_ = 0;
    mutable int errors_ = 0;
    std::string name_;
    std::string text_;
    std::vector<std::string> files_;
    std::vector<Line> lines_;
    std::vector<Pack> packs_;

    mutable std::vector<std::size_t> lineStarts_;
    void indexLines() const;

    void lineAt(std::size_t pos, int *line, std::size_t *start) const;
};
