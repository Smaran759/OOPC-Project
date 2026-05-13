#include <ncurses.h>
#include <string>
#include <vector>
#include <fstream>
#include <map>
#include <ctime>
#include <algorithm>
#include <stdexcept>
#include <sys/stat.h>

// ═══════════════════════════════════════════════════════════════
//  STRUCT: Entry
//  Plain data bundle — no behavior, just holds one record's data
// ═══════════════════════════════════════════════════════════════
struct Entry {
    std::string caption;
    std::string timestamp;
    double      amount;
};

// ═══════════════════════════════════════════════════════════════
//  CLASS: Logger
//  CONCEPT: Static functions + Encapsulation
//  All members are static — Logger is never instantiated.
//  It acts as a global service that any class can call.
//  Private data (errorCount, logFile) cannot be touched outside.
// ═══════════════════════════════════════════════════════════════
class Logger {
    // PRIVATE — no outside code can read or write these directly
    static int         errorCount;   // shared across ALL Logger uses (static)
    static std::string logFile;

public:
    // STATIC FUNCTION — called as Logger::logError(...), no object needed
    static void logError(const std::string &ctx, const std::string &msg) {
        errorCount++;
        std::ofstream f(logFile, std::ios::app);   // append mode
        if (!f.is_open()) return;
        time_t now = time(nullptr);
        char   buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
        f << "[" << buf << "] " << ctx << ": " << msg << "\n";
    }

    // STATIC FUNCTION — read-only access to private errorCount
    static int  getErrorCount() { return errorCount; }
    static void resetCount()    { errorCount = 0;    }
};

// Static members must be defined outside the class body
int         Logger::errorCount = 0;
std::string Logger::logFile    = "ledger_errors.log";


// ═══════════════════════════════════════════════════════════════
//  CLASS: FileHandler  (Abstract Base Class)
//  CONCEPT: Abstraction + Inheritance base + Polymorphism
//
//  This class defines WHAT a file-handling object must do,
//  but not HOW. Subclasses supply the HOW via override.
//  You cannot create a FileHandler directly — it has pure
//  virtual functions (= 0), making it abstract.
// ═══════════════════════════════════════════════════════════════
class FileHandler {
protected:
    // PROTECTED — subclasses (Ledger, BackupManager) can read/write
    // filepath directly. Outside code cannot.
    std::string filepath;

    // PROTECTED helper: formats one entry into a tab-separated line.
    // Shared implementation that both subclasses can reuse.
    std::string formatEntry(const Entry &e) const {
        return std::to_string(e.amount)
             + "\t" + e.caption
             + "\t" + e.timestamp
             + "\n";
    }

public:
    // Constructor — subclasses pass their file path up via : FileHandler(path)
    explicit FileHandler(const std::string &path) : filepath(path) {}

    // Virtual destructor — essential when deleting via base pointer
    virtual ~FileHandler() {}

    // PURE VIRTUAL — no implementation here, forces every subclass to define these
    // CONCEPT: Polymorphism — same call (save/load) behaves differently per subclass
    virtual void save() const = 0;
    virtual void load()       = 0;

    std::string getFilepath() const { return filepath; }

    // STATIC FUNCTION — utility that belongs to the concept of file handling,
    // not to any specific instance. Called as FileHandler::fileExists(...)
    static bool fileExists(const std::string &path) {
        std::ifstream f(path);
        return f.is_open();
    }
};


// ═══════════════════════════════════════════════════════════════
//  Forward declaration for friend function
// ═══════════════════════════════════════════════════════════════
class Ledger;
void printLedgerStats(const Ledger &l, WINDOW *win, int startRow);


// ═══════════════════════════════════════════════════════════════
//  CLASS: Ledger
//  CONCEPT: Inheritance, Encapsulation, Friend function,
//           Polymorphism (override), Error handling
//
//  Inherits FileHandler publicly — Ledger IS-A FileHandler.
//  Owns all entry data privately (Encapsulation).
//  Overrides save/load with its specific format (Polymorphism).
// ═══════════════════════════════════════════════════════════════
class Ledger : public FileHandler {     // INHERITANCE — public means FileHandler's
                                        // public members stay public in Ledger

    // PRIVATE — only Ledger's own methods can touch entries.
    // CONCEPT: Encapsulation — data is hidden, access goes through methods.
    std::vector<Entry> entries;

    // FRIEND FUNCTION — grants printLedgerStats access to private entries.
    // CONCEPT: Friend function — bypasses normal access rules for one specific function.
    // It is declared here but defined outside the class.
    friend void printLedgerStats(const Ledger &l, WINDOW *win, int startRow);

public:
    double discountPercent = 0.0;

    // Constructor calls FileHandler's constructor with the filename,
    // then immediately loads saved data
    Ledger() : FileHandler("ledger_log.txt") {
        load();   // polymorphic call — resolves to Ledger::load()
    }

    // Destructor — auto-saves when object goes out of scope (RAII)
    ~Ledger() { save(); }

    // ── Mutations ───────────────────────────────────────────────
    void append(const std::string &caption, double amount) {
        time_t now   = time(nullptr);
        tm    *local = localtime(&now);
        char   buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", local);
        entries.push_back({caption, std::string(buf), amount});
    }

    bool edit(int idx, const std::string &caption, double amount) {
        if (idx < 0 || idx >= (int)entries.size()) return false;
        entries[idx].caption = caption;
        entries[idx].amount  = amount;
        return true;
    }

    // Returns a copy of entries so BackupManager can save them
    // Returning by value (copy) protects the internal vector
    std::vector<Entry> getEntries() const { return entries; }

    void clearEntries() {
        entries.clear();
        // Save immediately so the file reflects the cleared state
        save();
    }

    // ── Calculations ────────────────────────────────────────────
    double subtotal() const {
        double s = 0;
        for (const auto &e : entries) s += e.amount;
        return s;
    }

    double tax() const {
        // 2% calculated per-entry individually, then summed
        double t = 0;
        for (const auto &e : entries) t += e.amount * 0.02;
        return t;
    }

    double discountAmount() const { return subtotal() * (discountPercent / 100.0); }
    double total()          const { return subtotal() + tax() - discountAmount();   }

    std::vector<std::pair<std::string, double>> captionTotals() const {
        std::map<std::string, double> m;
        for (const auto &e : entries) m[e.caption] += e.amount;
        return { m.begin(), m.end() };
    }

    int          count()    const { return (int)entries.size(); }
    const Entry &get(int i) const { return entries[i];          }

    // ── Polymorphic overrides ────────────────────────────────────
    // CONCEPT: Polymorphism — FileHandler declared these pure virtual,
    // Ledger provides its own concrete implementation.
    // CONCEPT: Error handling — try/catch guards every file operation.

    void save() const override {
        try {
            std::ofstream f(filepath);
            if (!f.is_open())
                throw std::runtime_error("Cannot open for writing: " + filepath);

            f << discountPercent << "\n";
            for (const auto &e : entries)
                f << formatEntry(e);   // uses protected helper from FileHandler

        } catch (const std::exception &ex) {
            // CONCEPT: Error handling — catch specific exception type
            Logger::logError("Ledger::save", ex.what());
        }
    }

    void load() override {
        try {
            std::ifstream f(filepath);
            if (!f.is_open()) return;   // first run — no file yet, not an error

            std::string line;

            // First line is the discount percent
            if (std::getline(f, line)) {
                try   { discountPercent = std::stod(line); }
                catch (...) { discountPercent = 0; }
            }

            while (std::getline(f, line)) {
                auto tab1 = line.find('\t');
                if (tab1 == std::string::npos) continue;
                auto tab2 = line.find('\t', tab1 + 1);

                double      amount = 0;
                std::string caption, timestamp;

                try {
                    amount = std::stod(line.substr(0, tab1));
                } catch (const std::exception &ex) {
                    // CONCEPT: Error handling — log bad lines, skip them
                    Logger::logError("Ledger::load", std::string("Bad amount: ") + ex.what());
                    continue;
                }

                if (tab2 == std::string::npos) {
                    caption   = line.substr(tab1 + 1);
                    timestamp = "";
                } else {
                    caption   = line.substr(tab1 + 1, tab2 - tab1 - 1);
                    timestamp = line.substr(tab2 + 1);
                }

                entries.push_back({caption, timestamp, amount});
            }

        } catch (const std::exception &ex) {
            Logger::logError("Ledger::load", ex.what());
        }
    }
};


// ═══════════════════════════════════════════════════════════════
//  FRIEND FUNCTION: printLedgerStats
//  CONCEPT: Friend function
//
//  Defined outside Ledger, but declared friend inside it.
//  This lets it access Ledger::entries directly — a private member
//  that no other outside code can touch.
//  Use case: computing stats (min, max, average) that need raw
//  access to the full entry list without exposing a getter.
// ═══════════════════════════════════════════════════════════════
void printLedgerStats(const Ledger &l, WINDOW *win, int startRow) {
    // Direct access to private member `entries` — only possible because friend
    int    count   = (int)l.entries.size();
    double highest = 0, lowest = 0, avg = 0;

    if (count > 0) {
        highest = lowest = l.entries[0].amount;
        for (const auto &e : l.entries) {
            if (e.amount > highest) highest = e.amount;
            if (e.amount < lowest)  lowest  = e.amount;
        }
        avg = l.subtotal() / count;
    }

    mvwprintw(win, startRow,     2, "Entries : %d",     count);
    mvwprintw(win, startRow + 1, 2, "Highest : %.2f",   highest);
    mvwprintw(win, startRow + 2, 2, "Lowest  : %.2f",   lowest);
    mvwprintw(win, startRow + 3, 2, "Average : %.2f",   avg);
}


// ═══════════════════════════════════════════════════════════════
//  CLASS: BackupManager
//  CONCEPT: Inheritance, Encapsulation, Static function,
//           Polymorphism (override), File handling, Error handling
//
//  Also inherits FileHandler — BackupManager IS-A FileHandler.
//  Its "file" is the index of all backup filenames.
//  Each backup itself is a separate timestamped file.
// ═══════════════════════════════════════════════════════════════
class BackupManager : public FileHandler {

    // PRIVATE — outside code sees none of this directly
    std::vector<std::string> backupFiles;            // list of backup paths
    const std::string        backupDir = "ledger_backups/";

public:
    BackupManager() : FileHandler("ledger_backups/backup_index.txt") {
        // Create backup directory if it doesn't exist
        mkdir("ledger_backups", 0755);   // POSIX — safe to call even if dir exists
        load();
    }

    ~BackupManager() { save(); }

    // ── Create a backup ─────────────────────────────────────────
    // Takes a snapshot of entries and writes them to a timestamped file.
    // Returns the filename so App can show it in a confirmation message.
    std::string createBackup(const std::vector<Entry> &entries, double discountPct) {
        try {
            time_t now   = time(nullptr);
            tm    *local = localtime(&now);
            char   buf[64];
            strftime(buf, sizeof(buf), "backup_%Y-%m-%d_%H-%M-%S.txt", local);
            std::string filename = backupDir + std::string(buf);

            std::ofstream f(filename);
            if (!f.is_open())
                throw std::runtime_error("Cannot create backup file: " + filename);

            f << discountPct << "\n";
            for (const auto &e : entries)
                // formatEntry is the protected helper inherited from FileHandler
                f << e.amount << "\t" << e.caption << "\t" << e.timestamp << "\n";

            backupFiles.push_back(filename);
            save();   // update index immediately
            return filename;

        } catch (const std::exception &ex) {
            Logger::logError("BackupManager::createBackup", ex.what());
            return "";
        }
    }

    // ── Load entries from a specific backup file ─────────────────
    // Returns a fresh vector — caller owns it, original is untouched
    std::vector<Entry> loadBackup(const std::string &filename) const {
        std::vector<Entry> result;
        try {
            std::ifstream f(filename);
            if (!f.is_open())
                throw std::runtime_error("Cannot open backup: " + filename);

            std::string line;
            std::getline(f, line);   // skip discount line

            while (std::getline(f, line)) {
                auto tab1 = line.find('\t');
                if (tab1 == std::string::npos) continue;
                auto tab2 = line.find('\t', tab1 + 1);

                double      amount = 0;
                std::string caption, timestamp;

                try   { amount = std::stod(line.substr(0, tab1)); }
                catch (...) { continue; }

                if (tab2 == std::string::npos) {
                    caption = line.substr(tab1 + 1);
                } else {
                    caption   = line.substr(tab1 + 1, tab2 - tab1 - 1);
                    timestamp = line.substr(tab2 + 1);
                }

                result.push_back({caption, timestamp, amount});
            }

        } catch (const std::exception &ex) {
            Logger::logError("BackupManager::loadBackup", ex.what());
        }
        return result;
    }

    // ── Delete every backup and the index ───────────────────────
    void deleteAllBackups() {
        for (const auto &f : backupFiles)
            remove(f.c_str());        // C standard lib — deletes file from disk
        backupFiles.clear();
        save();                       // write empty index
    }

    // ── Read-only accessors (Encapsulation — no direct vector access) ──
    const std::vector<std::string> &getBackupFiles() const { return backupFiles; }
    int backupCount() const { return (int)backupFiles.size(); }

    // STATIC FUNCTION — doesn't need an instance, purely a string utility.
    // Converts "ledger_backups/backup_2026-05-12_14-30-00.txt"
    //       to "2026-05-12_14-30-00" for clean display.
    static std::string displayName(const std::string &path) {
        // Find the last slash to strip the directory prefix
        auto slash = path.rfind('/');
        std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
        // Strip "backup_" prefix
        if (name.size() > 7 && name.substr(0, 7) == "backup_")
            name = name.substr(7);
        // Strip ".txt" suffix
        if (name.size() > 4 && name.substr(name.size() - 4) == ".txt")
            name = name.substr(0, name.size() - 4);
        return name;
    }

    // ── Polymorphic overrides ────────────────────────────────────
    // CONCEPT: Polymorphism — same virtual interface, different behavior.
    // Ledger::save writes entries; BackupManager::save writes the index.

    void save() const override {
        try {
            std::ofstream f(filepath);
            if (!f.is_open())
                throw std::runtime_error("Cannot write backup index: " + filepath);
            for (const auto &fn : backupFiles)
                f << fn << "\n";
        } catch (const std::exception &ex) {
            Logger::logError("BackupManager::save", ex.what());
        }
    }

    void load() override {
        try {
            std::ifstream f(filepath);
            if (!f.is_open()) return;   // no index yet — first run
            std::string line;
            while (std::getline(f, line)) {
                // Only add if the file still actually exists on disk
                if (!line.empty() && FileHandler::fileExists(line))
                    backupFiles.push_back(line);
            }
        } catch (const std::exception &ex) {
            Logger::logError("BackupManager::load", ex.what());
        }
    }
};


// ═══════════════════════════════════════════════════════════════
//  CLASS: App
//  Owns the TUI. Calls into Ledger and BackupManager.
//  No file I/O here — that concern belongs to the other classes.
// ═══════════════════════════════════════════════════════════════
class App {
    Ledger        ledger;
    BackupManager backupMgr;
    bool          showSummary = false;
    int           scroll      = 0;

    struct Cmd { const char *key; const char *label; };
    const std::vector<Cmd> cmds = {
        {"[A]","Append"}, {"[E]","Edit"},     {"[G]","Group"},
        {"[T]","Summary"},{"[D]","Discount"}, {"[S]","Stats"},
        {"[C]","Clear"},  {"[B]","Backups"},  {"[R]","Reset"},
        {"[↑↓]","Scroll"},{"[Q]","Quit"}
    };

public:
    void run() {
        initscr();
        cbreak();
        noecho();
        curs_set(0);
        keypad(stdscr, TRUE);
        start_color();

        init_pair(1, COLOR_BLACK,   COLOR_CYAN);
        init_pair(2, COLOR_CYAN,    COLOR_BLACK);
        init_pair(3, COLOR_YELLOW,  COLOR_BLACK);
        init_pair(4, COLOR_GREEN,   COLOR_BLACK);
        init_pair(5, COLOR_RED,     COLOR_BLACK);
        init_pair(6, COLOR_MAGENTA, COLOR_BLACK);
        init_pair(7, COLOR_WHITE,   COLOR_BLACK);

        while (true) {
            draw();
            int ch = getch();
            if      (ch == 'q' || ch == 'Q') break;
            else if (ch == 'a' || ch == 'A') promptAppend();
            else if (ch == 'e' || ch == 'E') promptEdit();
            else if (ch == 'c' || ch == 'C') clearWithBackup();
            else if (ch == 't' || ch == 'T') showSummary = !showSummary;
            else if (ch == 'g' || ch == 'G') showCaptionTotals();
            else if (ch == 'd' || ch == 'D') promptDiscount();
            else if (ch == 's' || ch == 'S') showStats();
            else if (ch == 'b' || ch == 'B') browseBackups();
            else if (ch == 'r' || ch == 'R') promptReset();
            else if (ch == KEY_DOWN)             scroll++;
            else if (ch == KEY_UP && scroll > 0) scroll--;
        }

        endwin();
    }

private:
    // ── Clear + auto-backup ─────────────────────────────────────
    void clearWithBackup() {
        if (ledger.count() == 0) return;

        // Snapshot entries into a backup before wiping
        std::string path = backupMgr.createBackup(ledger.getEntries(), ledger.discountPercent);
        ledger.clearEntries();

        // Brief on-screen confirmation
        int maxY, maxX;
        getmaxyx(stdscr, maxY, maxX);
        attron(COLOR_PAIR(4) | A_BOLD);
        mvprintw(maxY - 2, 2, " Backup saved: %-40s ",
            BackupManager::displayName(path).c_str());
        attroff(COLOR_PAIR(4) | A_BOLD);
        refresh();
        napms(1400);
    }

    // ── Reset everything ────────────────────────────────────────
    void promptReset() {
        WINDOW *win = makePopup(7, 50, "Reset Everything");
        mvwprintw(win, 3, 2, "Delete all entries AND all backups? [y/N]: ");
        wrefresh(win);

        int ch = wgetch(win);
        delwin(win);

        if (ch == 'y' || ch == 'Y') {
            ledger.clearEntries();
            backupMgr.deleteAllBackups();
            scroll = 0;
        }
    }

    // ── Stats popup (uses friend function) ──────────────────────
    void showStats() {
        WINDOW *win = makePopup(10, 36, "Ledger Stats");
        // printLedgerStats is the friend function — it accesses
        // Ledger's private entries directly for min/max/avg
        printLedgerStats(ledger, win, 3);
        mvwprintw(win, 8, 2, "Press any key to close");
        wrefresh(win);
        wgetch(win);
        delwin(win);
    }

    // ── Browse backups list ──────────────────────────────────────
    void browseBackups() {
        const auto &files = backupMgr.getBackupFiles();
        int total = (int)files.size();

        int maxY, maxX;
        getmaxyx(stdscr, maxY, maxX);

        if (total == 0) {
            WINDOW *win = makePopup(5, 38, "Backups");
            mvwprintw(win, 3, 2, "No backups yet. Press any key.");
            wrefresh(win);
            wgetch(win);
            delwin(win);
            return;
        }

        int winH      = std::min(total + 7, maxY - 4);
        int winW      = 54;
        int listRows  = winH - 6;
        int sel       = 0;
        int popScroll = 0;

        WINDOW *win = makePopup(winH, winW, "Previous Backups");
        keypad(win, TRUE);

        while (true) {
            // Keep selected row visible
            int maxPopScroll = std::max(0, total - listRows);
            if (popScroll > maxPopScroll) popScroll = maxPopScroll;
            if (sel < popScroll)               popScroll = sel;
            if (sel >= popScroll + listRows)   popScroll = sel - listRows + 1;

            wattron(win, A_UNDERLINE);
            mvwprintw(win, 2, 2, "%-46s", "Backup");
            wattroff(win, A_UNDERLINE);

            for (int i = 0; i < listRows; i++) {
                int idx = i + popScroll;
                // Clear the row first
                mvwprintw(win, 3 + i, 1, "%-*s", winW - 2, "");
                if (idx >= total) continue;

                if (idx == sel) wattron(win, A_REVERSE);
                mvwprintw(win, 3 + i, 2, "%-46s",
                    BackupManager::displayName(files[idx]).c_str());
                if (idx == sel) wattroff(win, A_REVERSE);
            }

            int fy = winH - 3;
            mvwprintw(win, fy, 2, "");
            wattron(win, A_BOLD); wprintw(win, "[↑↓]"); wattroff(win, A_BOLD);
            wprintw(win, " Select  ");
            wattron(win, A_BOLD); wprintw(win, "[Enter]"); wattroff(win, A_BOLD);
            wprintw(win, " View  ");
            wattron(win, A_BOLD); wprintw(win, "[Q]"); wattroff(win, A_BOLD);
            wprintw(win, " Close  ");
            wrefresh(win);

            int ch = wgetch(win);
            if (ch == 'q' || ch == 'Q' || ch == 27) break;
            else if (ch == KEY_DOWN && sel < total - 1) sel++;
            else if (ch == KEY_UP   && sel > 0)         sel--;
            else if (ch == '\n' || ch == KEY_ENTER) {
                delwin(win);
                viewBackup(files[sel]);
                // Recreate the list window after returning from viewer
                win = makePopup(winH, winW, "Previous Backups");
                keypad(win, TRUE);
            }
        }

        delwin(win);
    }

    // ── View a single backup's entries ───────────────────────────
    void viewBackup(const std::string &filename) {
        // CONCEPT: File handling — BackupManager reads the file,
        // App just displays what it gets back
        auto entries  = backupMgr.loadBackup(filename);
        int  total    = (int)entries.size();

        int maxY, maxX;
        getmaxyx(stdscr, maxY, maxX);

        int winH      = maxY - 4;
        int winW      = std::min(maxX - 4, 70);
        int listRows  = winH - 6;
        int popScroll = 0;

        std::string title = "Backup: " + BackupManager::displayName(filename);
        WINDOW *win = makePopup(winH, winW, title.c_str());
        keypad(win, TRUE);

        while (true) {
            int maxPopScroll = std::max(0, total - listRows);
            if (popScroll > maxPopScroll) popScroll = maxPopScroll;

            wattron(win, A_UNDERLINE);
            mvwprintw(win, 2, 2, "%-4s  %-26s  %12s", "#", "Caption", "Amount");
            wattroff(win, A_UNDERLINE);

            for (int i = 0; i < listRows; i++) {
                int idx = i + popScroll;
                mvwprintw(win, 3 + i, 2, "%-*s", winW - 4, "");
                if (idx >= total) continue;
                mvwprintw(win, 3 + i, 2, "%-4d  %-26s", idx + 1, entries[idx].caption.c_str());
                wattron(win, COLOR_PAIR(3));
                mvwprintw(win, 3 + i, 35, "%12.2f", entries[idx].amount);
                wattroff(win, COLOR_PAIR(3));
            }

            // Running total of the backup
            double bTotal = 0;
            for (const auto &e : entries) bTotal += e.amount;
            wattron(win, COLOR_PAIR(4) | A_BOLD);
            mvwprintw(win, winH - 3, 2, "%-30s %12.2f", "Backup Total", bTotal);
            wattroff(win, COLOR_PAIR(4) | A_BOLD);

            int fy = winH - 2;
            wattron(win, A_BOLD); mvwprintw(win, fy, 2, "[↑↓]"); wattroff(win, A_BOLD);
            wprintw(win, " Scroll  ");
            wattron(win, A_BOLD); wprintw(win, "[Q]"); wattroff(win, A_BOLD);
            wprintw(win, " Close       ");
            wrefresh(win);

            int ch = wgetch(win);
            if (ch == 'q' || ch == 'Q' || ch == 27) break;
            else if (ch == KEY_DOWN && popScroll < maxPopScroll) popScroll++;
            else if (ch == KEY_UP   && popScroll > 0)           popScroll--;
        }

        delwin(win);
    }

    // ── Status bar with bold key labels ─────────────────────────
    void drawStatusBar(int y, int maxX) {
        attron(COLOR_PAIR(1));
        mvprintw(y, 0, "%-*s", maxX, "");
        move(y, 1);
        for (const auto &cmd : cmds) {
            attron(A_BOLD);
            printw("%s", cmd.key);
            attroff(A_BOLD);
            printw("%s ", cmd.label);
        }
        attroff(COLOR_PAIR(1));
    }

    // ── Main draw ────────────────────────────────────────────────
    void draw() {
        clear();
        int maxY, maxX;
        getmaxyx(stdscr, maxY, maxX);

        // Title
        attron(COLOR_PAIR(1) | A_BOLD);
        mvprintw(0, 0, "%-*s", maxX, "  Ledger");
        attroff(COLOR_PAIR(1) | A_BOLD);

        // Backup count badge
        if (backupMgr.backupCount() > 0) {
            attron(COLOR_PAIR(1) | A_BOLD);
            mvprintw(0, maxX - 18, " Backups: %-3d      ", backupMgr.backupCount());
            attroff(COLOR_PAIR(1) | A_BOLD);
        }

        // Column headers
        attron(COLOR_PAIR(2) | A_UNDERLINE);
        mvprintw(2, 2, "%-4s  %-24s  %12s  %16s", "#", "Caption", "Amount", "Date");
        attroff(COLOR_PAIR(2) | A_UNDERLINE);

        int summaryRows = showSummary ? 7 : 0;
        int listStart   = 3;
        int listEnd     = maxY - 2 - summaryRows;
        int visible     = listEnd - listStart;

        int maxScroll = std::max(0, ledger.count() - visible);
        if (scroll > maxScroll) scroll = maxScroll;

        for (int i = 0; i < visible; i++) {
            int idx = i + scroll;
            if (idx >= ledger.count()) break;
            const Entry &e = ledger.get(idx);
            mvprintw(listStart + i, 2, "%-4d  %-24s  ", idx + 1, e.caption.c_str());
            attron(COLOR_PAIR(3));  printw("%12.2f", e.amount);  attroff(COLOR_PAIR(3));
            attron(COLOR_PAIR(6));  printw("  %16s", e.timestamp.c_str());  attroff(COLOR_PAIR(6));
        }

        // Summary block
        if (showSummary) {
            int sy = maxY - 2 - summaryRows;
            attron(COLOR_PAIR(2));  mvhline(sy, 0, ACS_HLINE, maxX);  attroff(COLOR_PAIR(2));  sy++;
            mvprintw(sy, 2, "%-36s", "Subtotal");
            attron(COLOR_PAIR(3));  printw("%12.2f", ledger.subtotal());  attroff(COLOR_PAIR(3));  sy++;
            mvprintw(sy, 2, "%-36s", "Tax (2% per entry)");
            attron(COLOR_PAIR(3));  printw("%12.2f", ledger.tax());  attroff(COLOR_PAIR(3));  sy++;
            mvprintw(sy, 2, "%-28s %6.1f%%", "Discount", ledger.discountPercent);
            attron(COLOR_PAIR(5));  printw("%12.2f", -ledger.discountAmount());  attroff(COLOR_PAIR(5));  sy++;
            attron(COLOR_PAIR(4) | A_BOLD);
            mvprintw(sy, 2, "%-36s%12.2f", "NET TOTAL", ledger.total());
            attroff(COLOR_PAIR(4) | A_BOLD);  sy++;
            attron(COLOR_PAIR(2));  mvhline(sy, 0, ACS_HLINE, maxX);  attroff(COLOR_PAIR(2));
        }

        drawStatusBar(maxY - 1, maxX);
        refresh();
    }

    // ── Helpers ─────────────────────────────────────────────────
    std::string getInput(WINDOW *win, int y, int x, int maxlen) {
        echo();
        curs_set(1);
        char buf[256] = {};
        mvwgetnstr(win, y, x, buf, maxlen);
        noecho();
        curs_set(0);
        return std::string(buf);
    }

    WINDOW *makePopup(int h, int w, const char *title) {
        int maxY, maxX;
        getmaxyx(stdscr, maxY, maxX);
        WINDOW *win = newwin(h, w, maxY / 2 - h / 2, maxX / 2 - w / 2);
        wbkgd(win, COLOR_PAIR(0));
        box(win, 0, 0);
        wattron(win, A_BOLD);
        mvwprintw(win, 1, 2, "%s", title);
        wattroff(win, A_BOLD);
        return win;
    }

    // ── Append ──────────────────────────────────────────────────
    void promptAppend() {
        WINDOW *win = makePopup(9, 48, "New Entry");
        mvwprintw(win, 3, 2, "Caption : ");
        mvwprintw(win, 5, 2, "Amount  : ");
        wrefresh(win);

        std::string caption   = getInput(win, 3, 12, 24);
        std::string amountStr = getInput(win, 5, 12, 16);
        delwin(win);

        if (caption.empty()) return;
        double amount = 0;
        try   { amount = std::stod(amountStr); }
        catch (...) { Logger::logError("App::promptAppend", "Bad amount: " + amountStr); }
        ledger.append(caption, amount);
    }

    // ── Edit ────────────────────────────────────────────────────
    void promptEdit() {
        WINDOW *win = makePopup(11, 50, "Edit Entry");
        mvwprintw(win, 3, 2, "Entry # : ");
        wrefresh(win);

        std::string idxStr = getInput(win, 3, 12, 6);
        int idx = -1;
        try { idx = std::stoi(idxStr) - 1; } catch (...) {}

        if (idx < 0 || idx >= ledger.count()) {
            mvwprintw(win, 5, 2, "Invalid number. Press any key.");
            wrefresh(win);
            wgetch(win);
            delwin(win);
            return;
        }

        const Entry &e = ledger.get(idx);
        mvwprintw(win, 5, 2, "Caption [%-20s]: ", e.caption.c_str());
        mvwprintw(win, 7, 2, "Amount  [%-8.2f          ]: ", e.amount);
        mvwprintw(win, 9, 2, "Leave blank to keep current");
        wrefresh(win);

        std::string newCaption   = getInput(win, 5, 30, 20);
        std::string newAmountStr = getInput(win, 7, 30, 12);
        delwin(win);

        std::string finalCaption = newCaption.empty() ? e.caption : newCaption;
        double      finalAmount  = e.amount;
        if (!newAmountStr.empty()) {
            try   { finalAmount = std::stod(newAmountStr); }
            catch (...) { Logger::logError("App::promptEdit", "Bad amount: " + newAmountStr); }
        }

        ledger.edit(idx, finalCaption, finalAmount);
    }

    // ── Caption group totals ─────────────────────────────────────
    void showCaptionTotals() {
        auto items = ledger.captionTotals();
        int  total = (int)items.size();

        int maxY, maxX;
        getmaxyx(stdscr, maxY, maxX);

        int winH      = std::min(total + 6, maxY - 4);
        int winW      = 48;
        int listRows  = winH - 5;
        int popScroll = 0;

        WINDOW *win = makePopup(winH, winW, "Totals by Caption");
        keypad(win, TRUE);

        while (true) {
            int maxPopScroll = std::max(0, total - listRows);
            if (popScroll > maxPopScroll) popScroll = maxPopScroll;

            wattron(win, A_UNDERLINE);
            mvwprintw(win, 2, 2, "%-28s %12s", "Caption", "Total");
            wattroff(win, A_UNDERLINE);

            for (int i = 0; i < listRows; i++)
                mvwprintw(win, 3 + i, 2, "%-*s", winW - 4, "");

            for (int i = 0; i < listRows; i++) {
                int idx = i + popScroll;
                if (idx >= total) break;
                mvwprintw(win, 3 + i, 2, "%-28s", items[idx].first.c_str());
                wattron(win, COLOR_PAIR(3));
                mvwprintw(win, 3 + i, 31, "%12.2f", items[idx].second);
                wattroff(win, COLOR_PAIR(3));
            }

            int fy = winH - 2;
            wattron(win, A_BOLD); mvwprintw(win, fy, 2, "[↑↓]"); wattroff(win, A_BOLD);
            wprintw(win, " Scroll  ");
            wattron(win, A_BOLD); wprintw(win, "[Q]"); wattroff(win, A_BOLD);
            wprintw(win, " Close          ");
            wrefresh(win);

            int ch = wgetch(win);
            if (ch == 'q' || ch == 'Q' || ch == 27) break;
            else if (ch == KEY_DOWN && popScroll < maxPopScroll) popScroll++;
            else if (ch == KEY_UP   && popScroll > 0)           popScroll--;
        }

        delwin(win);
    }

    // ── Discount ────────────────────────────────────────────────
    void promptDiscount() {
        WINDOW *win = makePopup(7, 44, "Set Discount");
        mvwprintw(win, 3, 2, "Discount %% [%5.1f%%] : ", ledger.discountPercent);
        mvwprintw(win, 5, 2, "Leave blank to keep current");
        wrefresh(win);

        std::string input = getInput(win, 3, 24, 8);
        delwin(win);

        if (!input.empty()) {
            try   { ledger.discountPercent = std::stod(input); }
            catch (...) { Logger::logError("App::promptDiscount", "Bad percent: " + input); }
        }
    }
};

// ═══════════════════════════════════════════════════════════════
//  main
// ═══════════════════════════════════════════════════════════════
int main() {
    App app;
    app.run();
    return 0;
}
